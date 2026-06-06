"""J.A.R.V.I.S. Step2: 安全なGitパイプライン自動化

master を最新化 → 新ブランチ作成 → 模擬修正 → msbuild検証
→ 成功時のみ commit & GitHub へ push。失敗時は push せず中断する。

手動で実行して動作確認するためのスクリプト。
"""

import subprocess
import sys
from datetime import datetime
from pathlib import Path

import ai_edit  # 同フォルダのAI編集モジュール

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

# コマンドを実行し結果を表示。失敗したら終了
def run(cmd, cwd=REPO_DIR):# cwdはカレントディレクトリでそれをリポジトリのディレクトリに設定
    print(f"\n$ {' '.join(str(c) for c in cmd)}")
    result = subprocess.run(cmd, cwd=str(cwd), text=True)
    if result.returncode != 0:
        raise SystemExit(f"コマンド失敗（終了コード {result.returncode}）")
    return result

# vswhereを使ってMSBuildのパスを取得
def find_msbuild():
    if not VSWHERE.exists():# existsは存在の確認
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
    run(["git", "checkout", BASE_BRANCH])# masterに切り替え
    run(["git", "pull", REMOTE, BASE_BRANCH])# pullして最新状態を取得

    # 2. 新ブランチを作成（JARVIS_作業内容_日時）
    #    作業内容(action)は将来のDiscordコマンド名と揃える: Refactor / EditClaude …
    #    第1引数で受け取り、無ければ動作確認用に "Test"
    stamp = datetime.now().strftime("%Y%m%d-%H%M")# 日時の取得
    action = sys.argv[1] if len(sys.argv) > 1 else "Test"# 作業内容
    branch = f"JARVIS_{action}_{stamp}"# ブランチ名を構築
    run(["git", "checkout", "-b", branch])# ブランチを作りチェックアウト

    # 3. AI編集：第2引数の指示文をAIに渡してファイルを書き換えさせる
    #    第3引数は対象ファイル（Ollama経路では必須、Claudeは自分で探すので省略可）
    #    指示が無ければ従来の配管テスト（マーカー追記）にフォールバック
    instruction = sys.argv[2] if len(sys.argv) > 2 else None
    target_file = sys.argv[3] if len(sys.argv) > 3 else None
    if instruction:
        ai_edit.run_ai_edit(action, instruction, REPO_DIR, target_file)
        message = instruction  # 指示文をそのままコミットメッセージに（B方式）
    else:
        with MARKER.open("a", encoding="utf-8") as f:
            f.write(f"{datetime.now().isoformat()} J.A.R.V.I.S. 配管テスト\n")
        message = f"J.A.R.V.I.S. 配管テスト {stamp}"
        print(f"指示なし。配管テストとしてマーカー追記: {MARKER}")

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
        print("\nビルド失敗。push せず中断します。")
        raise SystemExit(build.returncode)

    print("\nビルド成功。変更を確認します。")
    # AIがどのファイルを触ったか不明なので全変更をステージ
    run(["git", "add", "-A"])
    # 変更ゼロなら commit せず終了（AIが何も変えなかった時の安全弁）
    # git diff --cached --quiet は「差分なし→0／差分あり→1」を返す
    staged = subprocess.run(["git", "diff", "--cached", "--quiet"], cwd=str(REPO_DIR))
    if staged.returncode == 0:
        print("変更なし。commit せず終了します。")
        return
    run(["git", "commit", "-m", message])# B方式のメッセージでコミット
    run(["git", "push", "-u", REMOTE, branch])# リモートへプッシュ

    print(f"\n完了：ビルド確認済みブランチ '{branch}' を GitHub へ push しました。")

# 直接の実行時のみmainを動かす
if __name__ == "__main__":
    main()
