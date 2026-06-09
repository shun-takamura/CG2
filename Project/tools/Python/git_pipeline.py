"""J.A.R.V.I.S. Step2/4: 安全なGitパイプライン自動化

1本の「セッションブランチ」に作業を積み上げる方式。
- master 上にいる時に初めて編集が来たら、master を最新化して JARVIS_ 枝を1本作る
- すでに JARVIS_ 枝にいる時は、その枝にそのまま積む（master へ戻らない＝枝が分裂しない）
- 各コマンドは「AI編集 → msbuild検証 → 成功時ローカルcommit」まで。push はしない
- たまった作業は push_session()（Discordの /push）でまとめて GitHub へ送る

失敗は SystemExit ではなく PipelineError を投げる（Botを巻き込んで落とさないため）。
単体でも動く（末尾の __main__）。
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
BUILD_LOG = OUT_DIR / "build_error.log"                 # ビルド失敗時のログ出力先

BASE_BRANCH = "master"                                  # 派生元ブランチ
REMOTE = "origin"                                       # push 先リモート
SESSION_PREFIX = "JARVIS_"                              # セッションブランチの目印

# vswhere（VSインストーラ付属）で msbuild の場所を探す
VSWHERE = Path(
    r"C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe"
)


# パイプライン内の失敗を表す例外。Bot側はこれを捕まえてスマホに通知する
class PipelineError(RuntimeError):
    def __init__(self, message, log_path=None):
        super().__init__(message)
        self.log_path = log_path  # ビルドエラーログ等の添付ファイルパス（あれば）


# git等のコマンドを実行。失敗したら PipelineError を投げる
def _run(cmd, cwd=REPO_DIR):
    print(f"\n$ {' '.join(str(c) for c in cmd)}")
    result = subprocess.run(cmd, cwd=str(cwd), text=True, capture_output=True)
    if result.stdout:
        print(result.stdout)
    if result.stderr:
        print(result.stderr)
    if result.returncode != 0:
        raise PipelineError(
            f"コマンド失敗: {' '.join(str(c) for c in cmd)}\n{result.stderr.strip()}"
        )
    return result


# 今いるブランチ名を返す
def _current_branch():
    result = subprocess.run(
        ["git", "rev-parse", "--abbrev-ref", "HEAD"],
        cwd=str(REPO_DIR), text=True, capture_output=True,
    )
    return result.stdout.strip()


# 失敗した編集を捨てて、ブランチを直前のコミット状態に戻す（セッションを汚さない）
def _discard_changes():
    subprocess.run(["git", "reset", "--hard", "HEAD"], cwd=str(REPO_DIR),
                   text=True, capture_output=True)
    subprocess.run(["git", "clean", "-fd"], cwd=str(REPO_DIR),
                   text=True, capture_output=True)  # 追跡外の新規ファイルも除去（.gitignore対象は残る）


# vswhereを使ってMSBuildのパスを取得
def find_msbuild():
    if not VSWHERE.exists():
        raise PipelineError(f"vswhere が見つかりません: {VSWHERE}")
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
        raise PipelineError("MSBuild.exe が見つかりませんでした")
    return path[0]


def run_pipeline(action="Test", instruction=None, target_file=None):
    """1コマンド分のパイプラインを実行する（push はしない）。
    成功時は {ok, branch, committed, note/message} の辞書を返す。
    失敗時は PipelineError を投げる（ビルド失敗なら .log_path にログを添える）。
    """
    # 1. セッションブランチを決める
    branch = _current_branch()
    if branch.startswith(SESSION_PREFIX):
        # すでにセッション中 → その枝にそのまま積む（masterに戻らない）
        print(f"既存セッションに積みます: {branch}")
    else:
        # 新セッション開始 → master を最新化して JARVIS_ 枝を1本作る
        _run(["git", "checkout", BASE_BRANCH])
        _run(["git", "pull", REMOTE, BASE_BRANCH])
        stamp = datetime.now().strftime("%Y%m%d-%H%M")
        branch = f"{SESSION_PREFIX}{action}_{stamp}"
        _run(["git", "checkout", "-b", branch])

    # 2. AI編集：指示があればAIに書き換えさせる。無ければ配管テスト（マーカー追記）
    if instruction:
        ai_edit.run_ai_edit(action, instruction, REPO_DIR, target_file)
        message = instruction  # 指示文をそのままコミットメッセージに（B方式）
    else:
        with MARKER.open("a", encoding="utf-8") as f:
            f.write(f"{datetime.now().isoformat()} J.A.R.V.I.S. 配管テスト\n")
        message = f"J.A.R.V.I.S. 配管テスト {datetime.now():%Y%m%d-%H%M}"
        print(f"指示なし。配管テストとしてマーカー追記: {MARKER}")

    # 3. msbuild でビルド検証（ログを捕捉して失敗時に添付できるようにする）
    msbuild = find_msbuild()
    print(f"\nMSBuild: {msbuild}")
    build = subprocess.run(
        [msbuild, str(SLN), "/p:Configuration=Debug", "/p:Platform=x64", "/m"],
        cwd=str(REPO_DIR),
        text=True,
        capture_output=True,
    )

    # 4. 安全弁：失敗なら編集を捨ててセッションを直前の状態に戻し、ログを残して中断
    if build.returncode != 0:
        BUILD_LOG.write_text(build.stdout or "", encoding="utf-8")
        tail = "\n".join((build.stdout or "").splitlines()[-15:])
        _discard_changes()  # 壊れた編集を破棄（commit済みの過去作業はそのまま残る）
        raise PipelineError(
            f"ビルド失敗（branch={branch}）。編集を破棄して中断。\n{tail}",
            log_path=BUILD_LOG,
        )

    # 5. 成功：ローカルにcommitだけ（pushはしない）
    print("\nビルド成功。変更をコミットします。")
    _run(["git", "add", "-A"])
    staged = subprocess.run(["git", "diff", "--cached", "--quiet"], cwd=str(REPO_DIR))
    if staged.returncode == 0:
        return {"ok": True, "branch": branch, "committed": False, "note": "変更なし（commitせず）"}

    _run(["git", "commit", "-m", message])
    print(f"\nコミット完了（未push）: {branch}")
    return {"ok": True, "branch": branch, "committed": True, "message": message}


def push_session():
    """今いるセッションブランチを GitHub へまとめて push する（Discordの /push）。"""
    branch = _current_branch()
    if not branch.startswith(SESSION_PREFIX):
        raise PipelineError(
            f"今 '{branch}' にいます。J.A.R.V.I.S.セッションブランチではないので push しません。"
        )
    _run(["git", "push", "-u", REMOTE, branch])
    print(f"\npush 完了: {branch}")
    return {"ok": True, "branch": branch}


# 直接実行:
#   python git_pipeline.py <action> "<instruction>" [<target_file>]   … 1作業を積む
#   python git_pipeline.py Push                                        … まとめてpush
if __name__ == "__main__":
    action = sys.argv[1] if len(sys.argv) > 1 else "Test"
    try:
        if action == "Push":
            print(f"結果: {push_session()}")
        else:
            instruction = sys.argv[2] if len(sys.argv) > 2 else None
            target_file = sys.argv[3] if len(sys.argv) > 3 else None
            print(f"結果: {run_pipeline(action, instruction, target_file)}")
    except PipelineError as e:
        print(f"\n中断: {e}")
        raise SystemExit(1)
