"""ビューポート内キャプチャ（D3）。

Modal Operator でカメラ/動画をバックグラウンドスレッドで読み、
3Dビューポートに骨格オーバーレイ付きのプレビューをピクチャーインピクチャー表示する。
停止後は rig.solve() → 一時 .gltf 書き出し → live_server.import_gltf() で
同一プロセス内から直接シーンへ取り込む（ネットワーク不要）。
"""

from __future__ import annotations

import tempfile
import threading
import time
from pathlib import Path

import bpy
import gpu
from gpu_extras.presets import draw_texture_2d

from . import live_server
from . import preferences

try:
    import cv2
    import numpy as np
    import capture_core  # mediapipe に依存するため vendor 未インストール時は失敗しうる
except ImportError:
    cv2 = None
    np = None
    capture_core = None

# これらは純Pythonで mediapipe/cv2 に依存しないので常にimport可能
import capture_project
import rig
import gltf_export


def _try_import_deps():
    """cv2/capture_core を（再）import する。

    Preferences から依存関係をインストールした場合、このモジュールは既に
    import 済み（cv2=None のまま）になっているため、初回失敗時のNoneを
    使い続けてしまう。invoke() の直前に呼び直すことで同一セッション内での
    インストール直後利用に対応する。
    """
    global cv2, np, capture_core
    if capture_core is not None:
        return True
    try:
        import cv2 as _cv2
        import numpy as _np
        import capture_core as _capture_core
    except ImportError:
        return False
    cv2 = _cv2
    np = _np
    capture_core = _capture_core
    return True

DISPLAY_WIDTH = 320
PREVIEW_DIR = Path(tempfile.gettempdir()) / "MediaPipeMocap_blender_preview"

_state = {"running": False, "stop_requested": False, "status": "", "source": "",
          "preview_error": ""}
_frame_lock = threading.Lock()
_latest_frame = {"data": None, "w": 0, "h": 0}
_collected_frames = []

_texture = None
_tex_size = (0, 0)


def _set_status(text):
    _state["status"] = text


def _push_latest_frame(frame_bgr):
    """プレビュー用フレームを保持する。

    GPUTexture は FLOAT 形式の Buffer しか受け付けないため、
    表示サイズへ縮小してから RGBA float(0..1) へ変換しておく
    （縮小してから変換することで毎フレームの変換コストも抑える）。
    """
    h, w = frame_bgr.shape[:2]
    disp_w = min(DISPLAY_WIDTH, w)
    disp_h = max(1, int(h * disp_w / w))
    small = cv2.resize(frame_bgr, (disp_w, disp_h), interpolation=cv2.INTER_AREA)
    rgba = cv2.cvtColor(small, cv2.COLOR_BGR2RGBA)
    rgba = np.flipud(rgba)  # OpenGL は左下原点
    data = np.ascontiguousarray(rgba, dtype=np.float32).ravel() / 255.0
    with _frame_lock:
        _latest_frame["data"] = data
        _latest_frame["h"], _latest_frame["w"] = disp_h, disp_w


def _capture_worker(source, camera_index, video_path):
    global _collected_frames
    _collected_frames = []
    try:
        landmarker = capture_core.create_landmarker(status_cb=_set_status)
    except Exception as e:
        _state["status"] = f"エラー: {e}"
        _state["running"] = False
        return

    if source == "webcam":
        cap = cv2.VideoCapture(camera_index, cv2.CAP_DSHOW)
    else:
        cap = cv2.VideoCapture(str(video_path))
    if not cap.isOpened():
        _state["status"] = "カメラ/動画を開けませんでした"
        _state["running"] = False
        landmarker.close()
        return

    fps = cap.get(cv2.CAP_PROP_FPS) or 30.0
    if fps <= 0:
        fps = 30.0
    total = int(cap.get(cv2.CAP_PROP_FRAME_COUNT)) if source == "video" else 0

    idx = 0
    last_ts = -1
    t0 = time.perf_counter()
    try:
        while not _state["stop_requested"]:
            ok, frame = cap.read()
            if not ok:
                break
            t = (time.perf_counter() - t0) if source == "webcam" else idx / fps
            ts_ms = max(int(t * 1000), last_ts + 1)
            last_ts = ts_ms
            lm = capture_core.detect(landmarker, frame, ts_ms)
            if lm is not None:
                _collected_frames.append((t, lm))
            _push_latest_frame(frame)
            if source == "video" and total > 0:
                _state["status"] = f"処理中… {idx}/{total} ({idx / total * 100:.0f}%)"
            else:
                _state["status"] = f"録画中… {t:.1f}s / {len(_collected_frames)} フレーム"
            idx += 1
    finally:
        cap.release()
        landmarker.close()
        _state["running"] = False


def _update_texture():
    global _texture, _tex_size
    with _frame_lock:
        data = _latest_frame["data"]
        w, h = _latest_frame["w"], _latest_frame["h"]
    if data is None or w == 0:
        return
    try:
        buf = gpu.types.Buffer("FLOAT", w * h * 4, data)
    except (TypeError, ValueError):
        # numpy 配列を直接受け付けない環境向けのフォールバック
        buf = gpu.types.Buffer("FLOAT", w * h * 4, data.tolist())
    if _texture is None or _tex_size != (w, h):
        _texture = gpu.types.GPUTexture((w, h), format="RGBA8", data=buf)
        _tex_size = (w, h)
    else:
        _texture.update(buf)


def _draw_callback():
    if _texture is None:
        return
    w, h = _tex_size
    if w == 0:
        return
    try:
        region = bpy.context.region
        x = max(0, region.width - w - 10)
        draw_texture_2d(_texture, (x, 10), w, h)
    except (AttributeError, RuntimeError, ValueError):
        pass  # プレビュー描画の失敗はキャプチャ本体に影響させない


def _process_result(context):
    """停止後: rig.solve → 一時glTF → シーンへ直接インポート → mocapdataも保存"""
    global _texture
    _texture = None
    if not _collected_frames:
        _state["status"] = "有効なフレームがありませんでした"
        return
    try:
        result = rig.solve(_collected_frames, smoothing=True)
    except ValueError as e:
        _state["status"] = f"エラー: {e}"
        return

    PREVIEW_DIR.mkdir(parents=True, exist_ok=True)
    out = PREVIEW_DIR / "capture.gltf"
    gltf_export.export_gltf(result, out)
    live_server.import_gltf(str(out), clear=True)

    mocap_path = capture_project.save(result, PREVIEW_DIR / "capture")
    _state["status"] = (
        f"キャプチャ完了: {len(result['times'])} フレーム / "
        f"{result['duration']:.1f}s（{mocap_path.name} に保存）")
    _state["last_mocap_path"] = str(mocap_path)


class MOCAP_OT_capture(bpy.types.Operator):
    bl_idname = "mocap.capture"
    bl_label = "MediaPipeキャプチャ"
    bl_options = {"REGISTER"}

    source: bpy.props.EnumProperty(
        name="入力ソース",
        items=[("webcam", "Webカメラ", ""), ("video", "動画ファイル", "")],
        default="webcam")
    camera_index: bpy.props.IntProperty(name="カメラ番号", default=0, min=0)
    video_path: bpy.props.StringProperty(name="動画ファイル", subtype="FILE_PATH")

    _timer = None
    _draw_handler = None

    def invoke(self, context, event):
        if not _try_import_deps() or not preferences.dependencies_available():
            self.report({"ERROR"},
                       "依存関係が未インストールです。Preferencesからインストールしてください")
            return {"CANCELLED"}
        if _state["running"]:
            self.report({"WARNING"}, "既にキャプチャ中です")
            return {"CANCELLED"}
        if self.source == "video" and not self.video_path:
            self.report({"ERROR"}, "動画ファイルを指定してください")
            return {"CANCELLED"}

        _state["running"] = True
        _state["stop_requested"] = False
        _state["status"] = "開始しています…"
        _state["source"] = self.source
        _state["preview_error"] = ""

        threading.Thread(
            target=_capture_worker,
            args=(self.source, self.camera_index, self.video_path),
            daemon=True).start()

        self._draw_handler = bpy.types.SpaceView3D.draw_handler_add(
            _draw_callback, (), "WINDOW", "POST_PIXEL")
        wm = context.window_manager
        self._timer = wm.event_timer_add(1 / 20, window=context.window)
        wm.modal_handler_add(self)
        return {"RUNNING_MODAL"}

    def modal(self, context, event):
        if event.type == "TIMER":
            # プレビュー更新の失敗でモーダルを落とさない（落とすと停止後の
            # rig.solve〜シーン取り込みが永久に実行されなくなる）
            try:
                _update_texture()
            except Exception as e:
                _state["preview_error"] = str(e)
            for area in context.screen.areas:
                if area.type == "VIEW_3D":
                    area.tag_redraw()
            if not _state["running"] or _state["stop_requested"]:
                return self._finish(context)
        elif event.type == "ESC":
            _state["stop_requested"] = True
            return self._finish(context)
        return {"PASS_THROUGH"}

    def _finish(self, context):
        _state["stop_requested"] = True
        for _ in range(100):  # ワーカースレッド終了を最大5秒待つ
            if not _state["running"]:
                break
            time.sleep(0.05)
        # ハンドラ解除は結果処理より先に、かつ失敗しても続行する
        try:
            if self._timer:
                context.window_manager.event_timer_remove(self._timer)
                self._timer = None
            if self._draw_handler:
                bpy.types.SpaceView3D.draw_handler_remove(self._draw_handler,
                                                          "WINDOW")
                self._draw_handler = None
        except (RuntimeError, ValueError):
            pass
        try:
            _process_result(context)
        except Exception as e:
            _state["status"] = f"エラー: {e}"
            self.report({"ERROR"}, str(e))
        for area in context.screen.areas:
            if area.type == "VIEW_3D":
                area.tag_redraw()
        return {"FINISHED"}


class MOCAP_OT_capture_from_video(bpy.types.Operator):
    """動画ファイルのドラッグ&ドロップから起動される（FileHandler経由）"""
    bl_idname = "mocap.capture_from_video"
    bl_label = "動画からMediaPipeキャプチャ"
    filepath: bpy.props.StringProperty(subtype="FILE_PATH")

    def execute(self, context):
        return bpy.ops.mocap.capture("INVOKE_DEFAULT",
                                     source="video", video_path=self.filepath)


class MOCAP_OT_stop_capture(bpy.types.Operator):
    bl_idname = "mocap.stop_capture"
    bl_label = "停止"

    @classmethod
    def poll(cls, context):
        return _state["running"]

    def execute(self, context):
        _state["stop_requested"] = True
        return {"FINISHED"}


class MOCAP_FH_video(bpy.types.FileHandler):
    bl_idname = "MOCAP_FH_video"
    bl_label = "MediaPipeMocap 動画ドロップ"
    bl_import_operator = "mocap.capture_from_video"
    bl_file_extensions = ".mp4;.mov;.avi;.mkv;.webm"

    @classmethod
    def poll_drop(cls, context):
        return context.area is not None and context.area.type == "VIEW_3D"


_classes = (MOCAP_OT_capture, MOCAP_OT_capture_from_video,
           MOCAP_OT_stop_capture, MOCAP_FH_video)


def register():
    for c in _classes:
        bpy.utils.register_class(c)


def unregister():
    _state["stop_requested"] = True
    for c in reversed(_classes):
        try:
            bpy.utils.unregister_class(c)
        except RuntimeError:
            pass
