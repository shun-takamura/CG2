"""アドオン Preferences — mediapipe/opencv のオンデマンドインストール。

Blender の Program Files 配下（管理者権限が要る）には触れず、
アドオン内の vendor/ フォルダへ `pip install --target` する。
Blender 内蔵 Python の sys.executable がそのまま同梱 python.exe を指すため、
パス探索は不要（実機確認済み）。
"""

from __future__ import annotations

import subprocess
import sys
import threading
from pathlib import Path

import bpy

_ADDON_DIR = Path(__file__).parent
_VENDOR_DIR = _ADDON_DIR / "vendor"
_REQUIRED_PACKAGES = ["mediapipe", "opencv-python"]

_install_state = {"running": False, "message": "", "done": False}


def dependencies_available() -> bool:
    """mediapipe / cv2 が現在 import 可能かどうか（vendor/ 追加後の判定）。"""
    try:
        import mediapipe  # noqa: F401
        import cv2  # noqa: F401
        return True
    except ImportError:
        return False


def _install_worker():
    _install_state["running"] = True
    _install_state["done"] = False
    _install_state["message"] = "インストール中…（数分かかる場合があります）"
    _VENDOR_DIR.mkdir(parents=True, exist_ok=True)
    try:
        result = subprocess.run(
            [sys.executable, "-m", "pip", "install",
             "--target", str(_VENDOR_DIR), *_REQUIRED_PACKAGES],
            capture_output=True, text=True, timeout=900)
        if result.returncode == 0:
            if str(_VENDOR_DIR) not in sys.path:
                sys.path.insert(0, str(_VENDOR_DIR))
            if dependencies_available():
                _install_state["message"] = "インストール完了"
            else:
                _install_state["message"] = (
                    "インストールは完了しましたが import に失敗しました。"
                    "Blenderを再起動してください。")
        else:
            _install_state["message"] = (
                f"インストール失敗 (code {result.returncode}):\n"
                f"{result.stderr[-500:]}")
    except (OSError, subprocess.SubprocessError) as e:
        _install_state["message"] = f"インストール失敗: {e}"
    finally:
        _install_state["running"] = False
        _install_state["done"] = True


class MOCAP_OT_install_dependencies(bpy.types.Operator):
    bl_idname = "mocap.install_dependencies"
    bl_label = "依存関係をインストール"
    bl_description = "mediapipe / opencv-python をアドオン内に個別インストールします"

    def execute(self, context):
        if _install_state["running"]:
            return {"CANCELLED"}
        threading.Thread(target=_install_worker, daemon=True).start()
        return {"FINISHED"}


class MediaPipeMocapPreferences(bpy.types.AddonPreferences):
    bl_idname = __package__

    def draw(self, context):
        layout = self.layout
        if dependencies_available():
            layout.label(text="依存関係: インストール済み", icon="CHECKMARK")
        else:
            layout.label(text="依存関係: 未インストール（mediapipe / opencv-python）",
                        icon="ERROR")
            row = layout.row()
            row.enabled = not _install_state["running"]
            row.operator("mocap.install_dependencies", icon="IMPORT")
        if _install_state["message"]:
            layout.label(text=_install_state["message"])


_classes = (MOCAP_OT_install_dependencies, MediaPipeMocapPreferences)


def register():
    for c in _classes:
        bpy.utils.register_class(c)


def unregister():
    for c in reversed(_classes):
        try:
            bpy.utils.unregister_class(c)
        except RuntimeError:
            pass
