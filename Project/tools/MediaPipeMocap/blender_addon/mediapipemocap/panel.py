"""View3D サイドバー「MoCap」タブ。

Phase C: ライブサーバの状態表示・開始/停止。
D3: ビューポート内キャプチャの操作（Webカメラ開始/停止、動画は本パネル上に
    ドラッグ&ドロップ — FileHandler は capture_operator.py 側で登録）。
D4 でリターゲット操作をここに追加する。
"""

from __future__ import annotations

import bpy

from . import live_server
from . import preferences
from . import capture_operator


class MOCAP_PT_panel(bpy.types.Panel):
    bl_label = "MediaPipeMocap"
    bl_space_type = "VIEW_3D"
    bl_region_type = "UI"
    bl_category = "MoCap"

    def draw(self, context):
        layout = self.layout

        if not preferences.dependencies_available():
            box = layout.box()
            box.label(text="依存関係が未インストールです", icon="ERROR")
            box.label(text="編集 > プリファレンス > アドオン から")
            box.label(text="インストールしてください")

        cap_box = layout.box()
        cap_box.label(text="キャプチャ", icon="CAMERA_DATA")
        state = capture_operator._state
        if state["running"]:
            cap_box.label(text=state["status"] or "実行中…", icon="RECORD_ON")
            cap_box.operator("mocap.stop_capture", icon="PAUSE")
            if state.get("preview_error"):
                cap_box.label(text="プレビュー表示エラー（記録は継続中）",
                             icon="ERROR")
        else:
            row = cap_box.row()
            op = row.operator("mocap.capture", text="Webカメラで開始", icon="CAMERA_DATA")
            op.source = "webcam"
            cap_box.label(text="動画ファイルはこのパネルにドラッグ&ドロップ")
            if state["status"]:
                cap_box.label(text=state["status"])

        self._draw_retarget(context, layout)

        box = layout.box()
        box.label(text="ライブ連携（外部アプリから受信）")
        if live_server.state["listening"]:
            box.label(text=f"リッスン中: {live_server.HOST}:{live_server.PORT}",
                     icon="RADIOBUT_ON")
            box.operator("mocap.stop_server", icon="PAUSE")
        else:
            box.label(text="停止中", icon="RADIOBUT_OFF")
            box.operator("mocap.start_server", icon="PLAY")
        if live_server.state["last"]:
            box.label(text=live_server.state["last"])

    def _draw_retarget(self, context, layout):
        st = context.scene.mocap_retarget
        box = layout.box()
        box.label(text="リターゲット", icon="ARMATURE_DATA")
        box.prop(st, "armature")

        box.prop(st, "source_mode")
        if st.source_mode == "file":
            box.prop(st, "mocap_path")

        box.prop(st, "bind_pose")
        if st.bind_pose == "a":
            box.prop(st, "arm_down_angle")

        row = box.row(align=True)
        row.prop(st, "reduce_keys")
        if st.reduce_keys:
            row.prop(st, "tolerance_deg", text="")

        # ---- ボーンマッピング ----
        map_box = box.box()
        header = map_box.row(align=True)
        header.prop(st, "show_mapping",
                    icon="TRIA_DOWN" if st.show_mapping else "TRIA_RIGHT",
                    text="", emboss=False)
        assigned = sum(1 for m in st.mappings if m.target)
        header.label(text=f"ボーンマッピング ({assigned}/{len(st.mappings)})"
                          if st.mappings else "ボーンマッピング")

        if st.show_mapping:
            btns = map_box.row(align=True)
            btns.operator("mocap.guess_mapping", icon="AUTO")
            btns.operator("mocap.clear_mapping", text="", icon="X")
            io = map_box.row(align=True)
            io.operator("mocap.save_mapping", text="保存", icon="FILE_TICK")
            io.operator("mocap.load_mapping", text="読込", icon="FILEBROWSER")

            if not st.mappings:
                map_box.operator("mocap.init_mapping", icon="PRESET_NEW")
            elif st.armature is None:
                map_box.label(text="アーマチュアを選択してください", icon="INFO")
            else:
                map_box.label(text="空欄 = 未割当（動かない）")
                for m in st.mappings:
                    row = map_box.row(align=True)
                    row.prop_search(m, "target", st.armature.data, "bones",
                                    text=m.source)
                    sub = row.row(align=True)
                    sub.active = bool(m.target)
                    sub.prop(m, "twist_deg", text="")

        box.operator("mocap.retarget", icon="PLAY")
        box.operator("mocap.reduce_keys", icon="DECORATE_KEYFRAME")


_classes = (MOCAP_PT_panel,)


def register():
    for c in _classes:
        bpy.utils.register_class(c)


def unregister():
    for c in reversed(_classes):
        try:
            bpy.utils.unregister_class(c)
        except RuntimeError:
            pass
