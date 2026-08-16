"""Blender アドオンパッケージのビルド。

- rig.py / capture_project.py / retarget.py / gltf_import.py / gltf_export.py /
  prism_export.py（アプリ本体、tkinter非依存の純ロジック）を
  blender_addon/mediapipemocap/mocap_core/ へ同期コピーする
- blender_addon/mediapipemocap/ をZIP化し、Blenderの「ディスクからインストール」
  で導入できる配布物を作る

使い方:
    python build_addon.py
"""

from __future__ import annotations

import shutil
import zipfile
from pathlib import Path

APP_DIR = Path(__file__).parent
ADDON_PKG_DIR = APP_DIR / "blender_addon" / "mediapipemocap"
MOCAP_CORE_DIR = ADDON_PKG_DIR / "mocap_core"

# アプリ本体側の「共有コア」= Blenderアドオンへ同期する純ロジックモジュール
CORE_MODULES = [
    "rig.py",
    "capture_project.py",
    "retarget.py",
    "gltf_import.py",
    "gltf_export.py",
    "prism_export.py",
    "capture_core.py",
    "keyreduce.py",
]

OUT_ZIP = APP_DIR / "MediaPipeMocap-blender-addon.zip"


def sync_core():
    MOCAP_CORE_DIR.mkdir(parents=True, exist_ok=True)
    (MOCAP_CORE_DIR / "__init__.py").write_text(
        '"""アプリ本体から同期されたコアロジック（build_addon.py が生成）。"""\n',
        encoding="utf-8")
    for name in CORE_MODULES:
        src = APP_DIR / name
        if not src.exists():
            raise FileNotFoundError(f"共有コアモジュールが見つかりません: {src}")
        shutil.copy(src, MOCAP_CORE_DIR / name)
    print(f"Synced {len(CORE_MODULES)} core modules -> {MOCAP_CORE_DIR}")


def make_zip():
    if OUT_ZIP.exists():
        OUT_ZIP.unlink()
    with zipfile.ZipFile(OUT_ZIP, "w", zipfile.ZIP_DEFLATED) as zf:
        for path in ADDON_PKG_DIR.rglob("*"):
            if path.is_dir():
                continue
            if ("__pycache__" in path.parts or "vendor" in path.parts
                    or "models" in path.parts):
                continue
            arcname = Path("mediapipemocap") / path.relative_to(ADDON_PKG_DIR)
            zf.write(path, arcname)
    print(f"Wrote {OUT_ZIP} ({OUT_ZIP.stat().st_size / 1024:.0f} KB)")


if __name__ == "__main__":
    sync_core()
    make_zip()
