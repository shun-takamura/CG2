"""MediaPipeMocap — Blender アドオン（パッケージ版）。

導入:
  Blender > 編集 > プリファレンス > アドオン > インストール
  → mediapipemocap フォルダを zip 化したものを選択 → 有効化

構成:
  live_server.py     — 外部アプリからのライブ受信（Phase C）
  capture_operator.py — ビューポート内キャプチャ（D3）
  retarget_operator.py — シーン内リターゲット（D4）
  panel.py            — View3D サイドバー「MoCap」タブ
  mocap_core/          — rig/retarget/gltf_import/gltf_export/prism_export/
                          capture_project の同期コピー（アプリ本体と共有、build_addon.py が同期）
  vendor/               — 「依存関係をインストール」で作成される mediapipe/opencv 等（gitignore対象）
"""

bl_info = {
    "name": "MediaPipeMocap",
    "author": "MediaPipeMocap",
    "version": (2, 0, 0),
    "blender": (4, 2, 0),
    "location": "View3D > サイドバー > MoCap",
    "description": "MediaPipe によるモーションキャプチャ・リターゲットをBlender内で完結",
    "category": "Import-Export",
}

import sys
from pathlib import Path

_ADDON_DIR = Path(__file__).parent
_VENDOR_DIR = _ADDON_DIR / "vendor"
_MOCAP_CORE_DIR = _ADDON_DIR / "mocap_core"


def _setup_paths():
    """vendor/（mediapipe等）と mocap_core/ を import できるようにする。"""
    for p in (str(_VENDOR_DIR), str(_MOCAP_CORE_DIR)):
        if Path(p).exists() and p not in sys.path:
            sys.path.insert(0, p)


_setup_paths()

from . import live_server  # noqa: E402
from . import preferences  # noqa: E402
from . import capture_operator  # noqa: E402
from . import retarget_operator  # noqa: E402
from . import panel  # noqa: E402

_SUBMODULES = (live_server, preferences, capture_operator, retarget_operator, panel)


def register():
    for m in _SUBMODULES:
        m.register()


def unregister():
    for m in reversed(_SUBMODULES):
        m.unregister()
