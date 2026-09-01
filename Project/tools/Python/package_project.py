"""提出用のクリーンなプロジェクトフォルダを作るツール

`.vs` / `Generated` / `Logs` などのビルド生成物・IDEキャッシュを除いた
「ソースとアセットだけ」の Project フォルダを出力する。

使い方（作業ディレクトリは Project/ でなくても動く）:
    python tools/Python/package_project.py                 # 既定の出力先へ
    python tools/Python/package_project.py --zip           # zip も作る
    python tools/Python/package_project.py --out <パス>     # 出力先を指定
    python tools/Python/package_project.py --check-only    # 登録チェックだけ

出力先の既定は package_release.py と同じ Distribution フォルダ:
    ../Generated/Output/Release/Distribution/Project/

除外は「ブラックリスト方式」。拾うファイルを列挙するのではなく、
落とすものだけを下の定数に書く。こうしておけば新規クラスを追加しても
このファイルを更新する必要がない（勝手に含まれる）。

ただし「フォルダに入っている」ことと「ビルド対象になっている」ことは別物なので、
DirectXGame/ 配下の .cpp/.h が vcxproj に登録されているかも突合して警告する。
"""

from __future__ import annotations

import argparse
import datetime
import os
import shutil
import sys
import xml.etree.ElementTree as ET
import zipfile
from pathlib import Path

# tools/Python/package_project.py -> tools/Python -> tools -> Project
PROJECT_ROOT = Path(__file__).resolve().parents[2]
REPO_ROOT = PROJECT_ROOT.parent
DEFAULT_OUT = REPO_ROOT / "Generated" / "Output" / "Release" / "Distribution" / "Project"

# ---------------------------------------------------------------------------
# 除外ルール
# ---------------------------------------------------------------------------

# この名前のディレクトリは配下ごと丸ごと落とす。
# 注意: "x64" "bin" "Debug" "Release" は externals/ の配布物パスに出てくるので入れてはいけない
#       （例: externals/DirectStorage/bin/x64、externals/assimp/lib/Release）。
EXCLUDE_DIR_NAMES = {
    ".vs",              # Visual Studio の IntelliSense キャッシュ（最大の肥大要因）
    ".git",
    ".github",
    ".claude",
    ".vscode",
    ".idea",
    "Generated",        # ビルド出力・中間ファイル・Assets.pack（ビルドで再生成される）
    "Logs",
    "Temp",
    "ipch",
    "__pycache__",
    ".mypy_cache",
    ".pytest_cache",
    "node_modules",
    "mergiraf",         # Gitのマージドライバ本体(exe 72MB)。ビルドにも実行にも要らない
}

# この拡張子のファイルは落とす。
# 注意: ".obj" はWavefrontモデル、".bin" はglTFのバッファなのでアセットとして必要。除外しない。
EXCLUDE_SUFFIXES = {
    ".pdb", ".ilk", ".exp", ".iobj", ".ipdb", ".tlog",   # ビルド副産物
    ".log", ".dmp",                                       # ログ・クラッシュダンプ
    ".zip",                                               # 過去の梱包物を巻き込まない
    ".user", ".suo", ".aps", ".ncb", ".opendb", ".sdf",   # VSのユーザー設定・キャッシュ
    ".cachefile", ".psess", ".vspx",
    ".pyc",
}

# 名前が完全一致したら落とす
EXCLUDE_FILE_NAMES = {
    ".env",             # APIキー等。絶対に配布物へ入れない
    "imgui.ini",        # ウィンドウ配置。実行時に再生成される
    "sync_cache.json",
    "desktop.ini",
    "Thumbs.db",
    ".DS_Store",
}

# Project/ からの相対パス（小文字・スラッシュ区切り）で個別に落とすもの
EXCLUDE_RELATIVE = {
    # CG2_0_1.vcxproj がリンクするのは mtd(Debug) と mt(Release) のみ。
    # md / mdd 版はどの構成からも参照されていないので約78MBまるごと不要。
    "externals/assimp/lib/debug/assimp-vc143-mdd.lib",
    "externals/assimp/lib/release/assimp-vc143-md.lib",
}

# ---------------------------------------------------------------------------
# vcxproj 登録チェック
# ---------------------------------------------------------------------------

MSBUILD_NS = "{http://schemas.microsoft.com/developer/msbuild/2003}"
VCXPROJ_FILES = [
    PROJECT_ROOT / "CG2_0_1.vcxproj",
    PROJECT_ROOT / "ArcanaEngine" / "ArcanaEngine.vcxproj",
]
SOURCE_ROOT = PROJECT_ROOT / "DirectXGame"
SOURCE_SUFFIXES = {".cpp", ".h", ".hpp"}

# 提出サイズの目安（教員の指示: 500MB超は差し戻し、300MB超も怪しい）
WARN_SIZE_MB = 300.0
LIMIT_SIZE_MB = 500.0


def normalized(path: Path) -> str:
    """Windowsのパス比較用に正規化した文字列を返す（大小文字と区切りを吸収）"""
    return os.path.normpath(str(path)).lower()


# vcxproj の ClCompile / ClInclude に登録されているソースの絶対パスを集める
def collect_registered_sources() -> dict[str, Path]:
    registered: dict[str, Path] = {}

    for proj in VCXPROJ_FILES:
        if not proj.exists():
            print(f"[WARN] vcxproj が見つかりません: {proj}", file=sys.stderr)
            continue

        # Include のパスは vcxproj 自身の位置からの相対（ArcanaEngine は ..\DirectXGame\... 形式）
        base = proj.parent
        tree = ET.parse(proj)

        for tag in ("ClCompile", "ClInclude"):
            for node in tree.iter(f"{MSBUILD_NS}{tag}"):
                include = node.get("Include")
                if not include:
                    continue
                absolute = base / include.replace("\\", "/")
                registered[normalized(absolute)] = absolute

    return registered


# DirectXGame 配下に実在するソースを集める
def collect_actual_sources() -> dict[str, Path]:
    actual: dict[str, Path] = {}
    if not SOURCE_ROOT.exists():
        return actual

    for f in SOURCE_ROOT.rglob("*"):
        if f.is_file() and f.suffix.lower() in SOURCE_SUFFIXES:
            actual[normalized(f)] = f

    return actual


# 「実在するが未登録」と「登録済みだが実在しない」を返す
def check_registration() -> tuple[list[Path], list[Path]]:
    registered = collect_registered_sources()
    actual = collect_actual_sources()

    # 未登録は DirectXGame 配下だけを対象にする（externals は登録済みのものだけ拾えばよい）
    unregistered = [actual[k] for k in sorted(actual.keys() - registered.keys())]

    # 幽霊登録は実在するかどうかで判定する。
    # registered には externals 配下も含まれるので、DirectXGame との差集合では判定できない。
    ghosts = [p for _, p in sorted(registered.items()) if not p.exists()]

    return unregistered, ghosts


def report_registration(unregistered: list[Path], ghosts: list[Path]) -> None:
    if not unregistered and not ghosts:
        print("[CHECK] vcxproj 登録: 問題なし")
        return

    if unregistered:
        print(f"[CHECK] 未登録のソースが {len(unregistered)} 件あります "
              f"（コピーはされますが、この構成のままではビルド対象になりません）")
        for f in unregistered:
            print(f"  ! {f.relative_to(PROJECT_ROOT).as_posix()}")

    if ghosts:
        print(f"[CHECK] vcxproj に登録されているのに実在しないファイルが {len(ghosts)} 件あります "
              f"（提出先でビルドエラーになります）")
        for f in ghosts:
            try:
                shown = f.resolve().relative_to(PROJECT_ROOT).as_posix()
            except ValueError:
                shown = str(f)
            print(f"  ! {shown}")


# ---------------------------------------------------------------------------
# ファイル収集とコピー
# ---------------------------------------------------------------------------

# 除外ルールに一つでも当たれば True
def is_excluded(file: Path, root: Path) -> bool:
    if file.suffix.lower() in EXCLUDE_SUFFIXES:
        return True

    if file.name in EXCLUDE_FILE_NAMES:
        return True

    relative = file.relative_to(root).as_posix().lower()
    if relative in EXCLUDE_RELATIVE:
        return True

    return False


# root 配下から提出対象のファイルを再帰的に集める
def collect_project_files(root: Path) -> list[Path]:
    files: list[Path] = []

    for dirpath, dirnames, filenames in os.walk(root):
        # 除外ディレクトリはここで刈り取る（配下を走査しない）
        dirnames[:] = [d for d in dirnames if d not in EXCLUDE_DIR_NAMES]

        current = Path(dirpath)
        for name in filenames:
            f = current / name
            if is_excluded(f, root):
                continue
            files.append(f)

    return files


def total_mb(files: list[Path]) -> float:
    return sum(f.stat().st_size for f in files) / (1024 * 1024)


# 出力先を空の状態にする。既定の出力先は使い捨てなので黙って作り直し、
# --out で指定された場所は事故防止のため --force を要求する。
def prepare_output_dir(out: Path, is_default: bool, force: bool) -> int:
    if not out.exists() or not any(out.iterdir()):
        return 0

    if is_default:
        shutil.rmtree(out)
        return 0

    if not force:
        print(f"[ERROR] 出力先に既にファイルがあります: {out}", file=sys.stderr)
        print("        中身を残したまま上書きすると混ざるため中断しました。", file=sys.stderr)
        print("        退避して作り直すなら --force を付けてください。", file=sys.stderr)
        return 1

    timestamp = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
    backup = out.with_name(f"{out.name}_旧_{timestamp}")
    out.rename(backup)
    print(f"[BACKUP] 既存フォルダを退避しました: {backup.name}")
    return 0


def copy_files(files: list[Path], src_root: Path, dst_root: Path) -> None:
    for f in files:
        target = dst_root / f.relative_to(src_root)
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(f, target)


# zip の中身は "Project/..." で始まる形にする（展開してそのまま提出フォルダに置ける）
def make_zip(files: list[Path], src_root: Path, zip_path: Path, top_name: str) -> None:
    if zip_path.exists():
        zip_path.unlink()

    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED, compresslevel=6) as zf:
        for f in files:
            arcname = f"{top_name}/{f.relative_to(src_root).as_posix()}"
            zf.write(f, arcname=arcname)


def report_size(label: str, size_mb: float) -> None:
    print(f"[SIZE] {label}: {size_mb:.1f} MB")

    if size_mb > LIMIT_SIZE_MB:
        print(f"  !! 500MB を超えています。差し戻し対象です。除外ルールを見直してください。")
    elif size_mb > WARN_SIZE_MB:
        print(f"  ! 300MB を超えています。実行ファイルセットと合算すると危険域です。")


def main() -> int:
    parser = argparse.ArgumentParser(
        description="提出用のクリーンなプロジェクトフォルダを作る")
    parser.add_argument("--out", type=Path, default=None,
                        help=f"出力先フォルダ（既定: {DEFAULT_OUT}）")
    parser.add_argument("--zip", action="store_true",
                        help="出力先の隣に zip も作る")
    parser.add_argument("--force", action="store_true",
                        help="--out の指定先に中身があっても退避して作り直す")
    parser.add_argument("--no-check", action="store_true",
                        help="vcxproj の登録チェックをしない")
    parser.add_argument("--strict", action="store_true",
                        help="登録チェックで問題があれば異常終了する")
    parser.add_argument("--check-only", action="store_true",
                        help="コピーせず登録チェックだけ実行する")
    args = parser.parse_args()

    print(f"[PACKAGE] source: {PROJECT_ROOT}")

    unregistered: list[Path] = []
    ghosts: list[Path] = []

    if not args.no_check:
        unregistered, ghosts = check_registration()
        report_registration(unregistered, ghosts)

    has_issue = bool(unregistered or ghosts)

    if args.check_only:
        return 1 if (args.strict and has_issue) else 0

    if args.strict and has_issue:
        print("[ERROR] --strict 指定のため中断しました。", file=sys.stderr)
        return 1

    is_default = args.out is None
    out = (DEFAULT_OUT if is_default else args.out).resolve()

    # 出力先が Project/ の中にあると自分自身をコピーし続けてしまう
    if out == PROJECT_ROOT or PROJECT_ROOT in out.parents:
        print(f"[ERROR] 出力先を Project フォルダの中に指定することはできません: {out}",
              file=sys.stderr)
        return 1

    if prepare_output_dir(out, is_default, args.force) != 0:
        return 1

    files = collect_project_files(PROJECT_ROOT)
    if not files:
        print("[ERROR] 対象ファイルが1件もありません。", file=sys.stderr)
        return 1

    size_mb = total_mb(files)
    print(f"[PACKAGE] {len(files)} 件を収集しました")

    out.mkdir(parents=True, exist_ok=True)
    copy_files(files, PROJECT_ROOT, out)
    print(f"[DONE] folder: {out}")
    report_size("プロジェクトフォルダ", size_mb)

    if args.zip:
        timestamp = datetime.datetime.now().strftime("%Y-%m-%d_%H-%M-%S")
        zip_path = out.parent / f"{out.name}_{timestamp}.zip"
        make_zip(files, PROJECT_ROOT, zip_path, out.name)
        zip_mb = zip_path.stat().st_size / (1024 * 1024)
        print(f"[DONE] zip:    {zip_path}")
        report_size("zip 圧縮後", zip_mb)

    return 0


if __name__ == "__main__":
    sys.exit(main())
