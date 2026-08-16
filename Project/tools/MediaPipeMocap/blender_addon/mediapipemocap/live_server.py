"""MediaPipeMocap Live Link — ローカルサーバ側の実装。

MediaPipeMocap アプリ（外部プロセス）からの glTF を、Blender セッションへ
ライブでインポートするTCPサーバを提供する（Phase C）。

プロトコル:
  改行区切り JSON 1 行: {"gltf": "C:/path/to.gltf", "clear": true}
  応答: "OK\n"（受信してキューに積んだ時点で即返す）

安全性:
  ソケット受信はバックグラウンドスレッド、実際の bpy 操作は
  bpy.app.timers（メインスレッド）で行う。bpy はメインスレッド限定のため。
"""

from __future__ import annotations

import json
import queue
import socket
import threading

import bpy

HOST = "127.0.0.1"
PORT = 47829
PREVIEW_COLLECTION = "MediaPipeMocap_Preview"

_server_thread = None
_server_sock = None
_stop_flag = threading.Event()
_job_queue = queue.Queue()
state = {"listening": False, "last": ""}


# ============================================================
# ソケットサーバ（バックグラウンドスレッド。bpy には触れない）
# ============================================================
def _serve():
    global _server_sock
    try:
        _server_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        _server_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        _server_sock.bind((HOST, PORT))
        _server_sock.listen(4)
        _server_sock.settimeout(0.5)
        state["listening"] = True
    except OSError as e:
        state["listening"] = False
        state["last"] = f"bind失敗: {e}"
        return

    while not _stop_flag.is_set():
        try:
            conn, _ = _server_sock.accept()
        except socket.timeout:
            continue
        except OSError:
            break
        with conn:
            try:
                conn.settimeout(2.0)
                buf = b""
                while b"\n" not in buf:
                    chunk = conn.recv(4096)
                    if not chunk:
                        break
                    buf += chunk
                line = buf.split(b"\n", 1)[0].decode("utf-8", "replace")
                msg = json.loads(line)
                _job_queue.put(msg)
                conn.sendall(b"OK\n")
            except (OSError, ValueError, json.JSONDecodeError) as e:
                state["last"] = f"受信エラー: {e}"
                try:
                    conn.sendall(b"ERR\n")
                except OSError:
                    pass

    state["listening"] = False
    try:
        _server_sock.close()
    except OSError:
        pass


def start_server():
    global _server_thread
    if _server_thread and _server_thread.is_alive():
        return
    _stop_flag.clear()
    _server_thread = threading.Thread(target=_serve, daemon=True)
    _server_thread.start()


def stop_server():
    _stop_flag.set()
    state["listening"] = False
    global _server_sock
    if _server_sock is not None:
        try:
            _server_sock.close()
        except OSError:
            pass


def queue_import(gltf_path, clear=True):
    """外部から直接呼べる投入口（同一プロセス内の D3 キャプチャ結果取り込み等に使う）。"""
    _job_queue.put({"gltf": str(gltf_path), "clear": clear})


# ============================================================
# インポート処理（メインスレッド: bpy.app.timers から呼ばれる）
# ============================================================
def _clear_preview():
    coll = bpy.data.collections.get(PREVIEW_COLLECTION)
    if coll is None:
        return
    for obj in list(coll.objects):
        data = obj.data
        is_armature = obj.type == "ARMATURE"
        anim = obj.animation_data
        action = anim.action if anim else None
        bpy.data.objects.remove(obj, do_unlink=True)
        if data is not None and data.users == 0:
            if is_armature:
                bpy.data.armatures.remove(data)
            elif hasattr(data, "users"):
                try:
                    bpy.data.meshes.remove(data)
                except (ReferenceError, RuntimeError):
                    pass
        if action is not None and action.users == 0:
            try:
                bpy.data.actions.remove(action)
            except (ReferenceError, RuntimeError):
                pass


def _ensure_preview_collection():
    coll = bpy.data.collections.get(PREVIEW_COLLECTION)
    if coll is None:
        coll = bpy.data.collections.new(PREVIEW_COLLECTION)
        bpy.context.scene.collection.children.link(coll)
    return coll


def import_gltf(path, clear):
    if clear:
        _clear_preview()

    before = set(bpy.data.objects)
    try:
        bpy.ops.import_scene.gltf(filepath=path, bone_heuristic="FORTUNE")
    except (RuntimeError, TypeError):
        # bone_heuristic 非対応の古いBlender等はフォールバック
        try:
            bpy.ops.import_scene.gltf(filepath=path)
        except RuntimeError as e2:
            state["last"] = f"インポート失敗: {e2}"
            return
    new_objs = [o for o in bpy.data.objects if o not in before]

    # 新規オブジェクトをプレビューコレクションへ移動
    coll = _ensure_preview_collection()
    for obj in new_objs:
        for c in list(obj.users_collection):
            if c is not coll:
                c.objects.unlink(obj)
        if obj.name not in coll.objects:
            coll.objects.link(obj)

    # 再生範囲をアニメーション長に合わせる（ベストエフォート）
    try:
        frame_end = 1
        for obj in new_objs:
            ad = obj.animation_data
            if ad and ad.action:
                frame_end = max(frame_end, int(ad.action.frame_range[1]))
        if frame_end > 1:
            bpy.context.scene.frame_start = 0
            bpy.context.scene.frame_end = frame_end
    except (AttributeError, RuntimeError):
        pass

    # ビューをフィット（ベストエフォート、コンテキスト不足なら黙って諦める）
    try:
        for area in bpy.context.screen.areas:
            if area.type == "VIEW_3D":
                for region in area.regions:
                    if region.type == "WINDOW":
                        with bpy.context.temp_override(area=area, region=region):
                            bpy.ops.view3d.view_all()
                        break
                break
    except (AttributeError, RuntimeError):
        pass

    state["last"] = f"インポート: {len(new_objs)} オブジェクト"


def drain_queue():
    """メインスレッドで定期実行。キューのジョブを処理する。"""
    try:
        while True:
            msg = _job_queue.get_nowait()
            path = msg.get("gltf")
            clear = bool(msg.get("clear", True))
            if path:
                import_gltf(path, clear)
    except queue.Empty:
        pass
    return 0.2  # 秒。次回も呼ばれるよう間隔を返す


# ============================================================
# Operators（サーバの開始/停止）
# ============================================================
class MOCAP_OT_start_server(bpy.types.Operator):
    bl_idname = "mocap.start_server"
    bl_label = "リッスン開始"

    def execute(self, context):
        start_server()
        return {"FINISHED"}


class MOCAP_OT_stop_server(bpy.types.Operator):
    bl_idname = "mocap.stop_server"
    bl_label = "リッスン停止"

    def execute(self, context):
        stop_server()
        return {"FINISHED"}


_classes = (MOCAP_OT_start_server, MOCAP_OT_stop_server)


def register():
    for c in _classes:
        bpy.utils.register_class(c)
    if not bpy.app.timers.is_registered(drain_queue):
        bpy.app.timers.register(drain_queue, persistent=True)
    start_server()


def unregister():
    stop_server()
    if bpy.app.timers.is_registered(drain_queue):
        bpy.app.timers.unregister(drain_queue)
    for c in reversed(_classes):
        try:
            bpy.utils.unregister_class(c)
        except RuntimeError:
            pass
