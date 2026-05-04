"""Releaseビルド成果物をzip化する配布パッケージングツール

Visual Studio の Release 構成のビルドイベントから呼び出される想定。
作業ディレクトリは $(ProjectDir) (= Project/) であること。

使い方（コマンドライン）:
    python tools/Python/package_release.py

zip 名は日付と時刻で管理（例: CG2_2026-05-04_15-32-45.zip）。
zip の中身はラッパーフォルダ無しのフラット構成:
    CG2_X_Y.exe
    dxcompiler.dll
    dxil.dll
    *.cso
    Resources/
        ...

Windows の「すべて展開」は zip ファイル名と同じ名前のフォルダを自動で作って
そこに中身を展開する。zip をリネームすれば展開後フォルダ名もそれに追従する。
"""

from __future__ import annotations

import argparse
import datetime
import sys
import zipfile
from pathlib import Path

# Project ルートからの相対パス（$(ProjectDir) で起動される）
RELEASE_DIR = Path("../Generated/Output/Release")
RESOURCES_DIR = Path("Resources")
OUTPUT_DIR = RELEASE_DIR / "Distribution"  # Release内の専用サブフォルダに出力

# Release ディレクトリから取り込む拡張子
INCLUDE_SUFFIXES = {".exe", ".dll", ".cso"}

# 取り込まない拡張子
EXCLUDE_SUFFIXES = {
    ".pdb", ".ilk", ".lib", ".exp",
    ".log", ".iobj", ".ipdb", ".obj",
    ".zip",
}

# Release直下のファイルを走査し必要なファイルを返す
def collect_release_files(release_dir: Path) -> list[Path]:
    files: list[Path] = []

    #フォルダ直下の中だけ走査
    for f in release_dir.iterdir():

        #ファイルでなければ早期リターン
        if not f.is_file():
            continue
        
        #大文字でも小文字でも扱えるようにする
        suffix = f.suffix.lower()

        #取り込まない拡張子のものだった場合早期リターン
        if suffix in EXCLUDE_SUFFIXES:
            continue
        
        #取り込まない拡張子に設定していなくても取り込むところに設定していなければ早期リターン
        if suffix not in INCLUDE_SUFFIXES:
            continue

        #ファイルを追加
        files.append(f)

    #まとめたものを返す
    return files

# Release配下を再帰的にすべて拾う
def collect_resources_files(resources_dir: Path) -> list[Path]:

    #なければ早期リターン
    if not resources_dir.exists():
        return []
    return [f for f in resources_dir.rglob("*") if f.is_file()]


def main() -> int:
    parser = argparse.ArgumentParser(description="Releaseビルド成果物をzip化")
    parser.add_argument("--silent", action="store_true",
                        help="ログ出力を最小限に")
    args = parser.parse_args()

    release_dir = RELEASE_DIR.resolve()
    resources_dir = RESOURCES_DIR.resolve()
    output_dir = OUTPUT_DIR.resolve()

    if not release_dir.exists():
        print(f"[ERROR] Release ディレクトリが見つかりません: {release_dir}", file=sys.stderr)
        return 1

    # 日付と時刻でタイムスタンプを作る（Windowsで使えるようコロンは使わない）
    timestamp = datetime.datetime.now().strftime("%Y-%m-%d_%H-%M-%S")
    zip_name = f"CG2_{timestamp}.zip"
    zip_path = output_dir / zip_name

    release_files = collect_release_files(release_dir)
    resources_files = collect_resources_files(resources_dir)

    if not release_files:
        print(f"[ERROR] Release ディレクトリに対象ファイルがありません: {release_dir}", file=sys.stderr)
        return 1

    output_dir.mkdir(parents=True, exist_ok=True)
    if zip_path.exists():
        zip_path.unlink()

    if not args.silent:
        print(f"[PACKAGE] {zip_path}")
        print(f"  timestamp: {timestamp}")
        print(f"  release:   {len(release_files)} 件")
        print(f"  resources: {len(resources_files)} 件")

    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED, compresslevel=6) as zf:
        for f in release_files:
            arcname = f.name
            zf.write(f, arcname=arcname)
            if not args.silent:
                print(f"  + {arcname}")

        for f in resources_files:
            rel = f.relative_to(resources_dir.parent)  # "Resources/..." の形にする
            arcname = rel.as_posix()
            zf.write(f, arcname=arcname)
            if not args.silent:
                print(f"  + {arcname}")

    size_mb = zip_path.stat().st_size / (1024 * 1024)
    print(f"[DONE] {zip_path.name} ({size_mb:.1f} MB)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
