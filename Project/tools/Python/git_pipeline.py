"""J.A.R.V.I.S. Step2: 安全なGitパイプライン自動化

master を最新化 → 新ブランチ作成 → 模擬修正 → msbuild検証
→ 成功時のみ commit & GitHub へ push。失敗時は push せず中断する。

手動で実行して動作確認するためのスクリプト。
"""

import subprocess
import sys
from datetime import datetime
from pathlib import Path

# tools/Python/git_pipeline.py を基準にフォルダを割り出す
OUT_DIR = Path(__file__).resolve().parent              # このフォルダ(tools/Python)
PROJECT_DIR = OUT_DIR.parent.parent                    # Project フォルダ
REPO_DIR = PROJECT_DIR.parent                          # リポジトリ直下(CG2)

SLN = PROJECT_DIR / "CG2_0_1.sln"                       # ビルド対象
MARKER = OUT_DIR / "jarvis_marker.txt"                  # 模擬修正を書き込む専用ファイル

BASE_BRANCH = "master"                                  # 派生元ブランチ
REMOTE = "origin"                                       # push 先リモート

# vswhere（VSインストーラ付属）で msbuild の場所を探す
VSWHERE = Path(
    r"C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe"
)


def run(cmd, cwd=REPO_DIR):
    """コマンドを実行し、結果を表示。失敗したら例外を投げる"""
    print(f"\n$ {' '.join(str(c) for c in cmd)}")
    result = subprocess.run(cmd, cwd=str(cwd), text=True)
    if result.returncode != 0:
        raise SystemExit(f"コマンド失敗（終了コード {result.returncode}）")
    return result


def find_msbuild():
    """vswhere を使って MSBuild.exe のフルパスを取得する"""
    if not VSWHERE.exists():
        raise SystemExit(f"vswhere が見つかりません: {VSWHERE}")
    result = subprocess.run(
        [
            str(VSWHERE),
            "-latest",
            "-requires", "Microsoft.Component.MSBuild",
            "-find", r"MSBuild\**\Bin\MSBuild.exe",
        ],
        text=True,
        capture_output=True,
    )
    path = result.stdout.strip().splitlines()
    if not path:
        raise SystemExit("MSBuild.exe が見つかりませんでした")
    return path[0]


def main():
    # 1. master へ切り替えて最新化
    run(["git", "checkout", BASE_BRANCH])
    run(["git", "pull", REMOTE, BASE_BRANCH])

    # 2. 新ブランチを作成（JARVIS_作業内容_日時）
    #    作業内容(action)は将来のDiscordコマンド名と揃える: Refactor / EditClaude …
    #    第1引数で受け取り、無ければ動作確認用に "Test"
    stamp = datetime.now().strftime("%Y%m%d-%H%M")
    action = sys.argv[1] if len(sys.argv) > 1 else "Test"
    branch = f"JARVIS_{action}_{stamp}"
    run(["git", "checkout", "-b", branch])

    # 3. 模擬修正：マーカーファイルに実行時刻を追記（ビルドに影響しない）
    with MARKER.open("a", encoding="utf-8") as f:
        f.write(f"{datetime.now().isoformat()} J.A.R.V.I.S. Step2 検証\n")
    print(f"模擬修正を書き込み: {MARKER}")

    # 4. msbuild でビルド検証
    msbuild = find_msbuild()
    print(f"\nMSBuild: {msbuild}")
    build = subprocess.run(
        [
            msbuild,
            str(SLN),
            "/p:Configuration=Debug",
            "/p:Platform=x64",
            "/m",
        ],
        cwd=str(REPO_DIR),
        text=True,
    )

    # 5. 安全弁：成功時のみ commit & push、失敗時は中断
    if build.returncode != 0:
        print("\n❌ ビルド失敗。push せず中断します。")
        raise SystemExit(build.returncode)

    print("\n✅ ビルド成功。commit & push します。")
    #    コミットメッセージは第2引数の指示文(B方式)。無ければ動作確認用の固定文
    message = sys.argv[2] if len(sys.argv) > 2 else f"J.A.R.V.I.S. Step2 自動検証 {stamp}"
    run(["git", "add", str(MARKER)])
    run(["git", "commit", "-m", message])
    run(["git", "push", "-u", REMOTE, branch])

    print(f"\n🎉 完了：ビルド確認済みブランチ '{branch}' を GitHub へ push しました。")


if __name__ == "__main__":
    main()
