"""PyInstaller で --onedir 形式の配布物を作るビルドスクリプト。

使い方:
    pip install pyinstaller
    python build_exe.py

dist/MediaPipeMocap/ にフォルダ一式が生成される。
mediapipe はネイティブライブラリ・内部データを含むため --collect-all で丸ごと
同梱する（デフォルトのフック任せだと一部リソースが欠落することがある）。
"""

from __future__ import annotations

import shutil
import subprocess
import sys
from pathlib import Path

APP_DIR = Path(__file__).parent

import build_addon  # noqa: E402 (mocap_core同期に使う)

args = [
    sys.executable, "-m", "PyInstaller",
    "--name", "MediaPipeMocap",
    "--onedir",
    "--windowed",
    "--noconfirm",
    "--collect-all", "mediapipe",
    "--collect-all", "cv2",
    str(APP_DIR / "main.py"),
]

print("Running:", " ".join(args))
subprocess.run(args, cwd=APP_DIR, check=True)

# models/ ディレクトリを配布物に同梱（空でも .gitkeep 相当のフォルダとして
# 用意しておき、初回起動時にそこへモデルを自動ダウンロードさせる）
dist_models = APP_DIR / "dist" / "MediaPipeMocap" / "models"
dist_models.mkdir(parents=True, exist_ok=True)

# README/LICENSE も同梱
for name in ("README.md", "LICENSE"):
    src = APP_DIR / name
    if src.exists():
        shutil.copy(src, APP_DIR / "dist" / "MediaPipeMocap" / name)

# blender_addon/ を同梱（Blender 自動起動のブートストラップが実行時に使う）
# mocap_core はアプリ本体から都度同期し、vendor/models（実行時生成物）は含めない
build_addon.sync_core()
dist_dir = APP_DIR / "dist" / "MediaPipeMocap"
addon_src = APP_DIR / "blender_addon"
if addon_src.exists():
    addon_dst = dist_dir / "blender_addon"
    if addon_dst.exists():
        shutil.rmtree(addon_dst)
    shutil.copytree(addon_src, addon_dst,
                    ignore=shutil.ignore_patterns("__pycache__", "vendor", "models"))

# Blender へインストールするためのアドオン zip も同梱する
# （Blender の「ディスクからインストール」はフォルダではなく zip を要求するため、
#  上の blender_addon/ とは別に、そのまま選べる zip を置いておく）
build_addon.make_zip()
shutil.copy(build_addon.OUT_ZIP, dist_dir / build_addon.OUT_ZIP.name)

print("\nDone. Output: dist/MediaPipeMocap/")
