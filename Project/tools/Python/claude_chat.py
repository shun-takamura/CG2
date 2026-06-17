"""J.A.R.V.I.S.: スマホから設計対話するための Claude CLI ラッパー

/ask       … 読み取り専用の単発相談（コード編集なし）
/plan      … 読み取り専用の設計対話（--resume で往復）
/plan_save … それまでの設計を Project/tools/JARVIS/Chat/<name>.md に書き出す

claude.exe は ai_edit.find_claude() を借りる（ai_edit 自体は変更しない）。
編集しないことは --permission-mode plan で、会話継続は --resume で担保する。
"""

import json
import subprocess
from pathlib import Path

import ai_edit  # find_claude を借りるだけ（ai_edit は変更しない）

# 設計メモの保存先（既存ツールの配置は触らず、新フォルダに集約）
#   このファイル: Project/tools/Python/claude_chat.py → parents[1] = Project/tools
CHAT_DIR = Path(__file__).resolve().parents[1] / "JARVIS" / "Chat"

CLAUDE_TIMEOUT = 600  # claude の応答待ち上限（秒）


# claude を叩いて (返答テキスト, session_id) を返す。JSON出力で構造化して受け取る
def _run_claude(args, cwd):
    claude = ai_edit.find_claude()
    result = subprocess.run(
        [claude, *args, "--output-format", "json"],
        cwd=str(cwd),
        text=True,
        encoding="utf-8",   # 日本語Windowsの既定(cp932)だとClaudeのUTF-8出力を読めず落ちる
        errors="replace",
        capture_output=True,
        timeout=CLAUDE_TIMEOUT,
    )
    if result.returncode != 0:
        msg = result.stderr.strip() or result.stdout.strip()
        raise RuntimeError(f"claude 実行失敗: {msg}")
    data = json.loads(result.stdout)
    return data.get("result", ""), data.get("session_id")


def ask(question, cwd):
    """単発の相談。plan モード＝コードを編集しない。返答テキストを返す。"""
    text, _ = _run_claude(["-p", question, "--permission-mode", "plan"], cwd)
    return text


def plan(instruction, cwd, resume_id=None):
    """設計対話。plan モード（編集なし）。(返答テキスト, session_id) を返す。
    resume_id を渡すと同じセッションを継続する。"""
    args = ["-p", instruction, "--permission-mode", "plan"]
    if resume_id:
        args += ["--resume", resume_id]
    return _run_claude(args, cwd)


def save_plan(name, cwd, resume_id):
    """それまでの設計対話の結論を Chat/<name>.md に Claude が書き出す（acceptEdits）。
    保存先パスと返答テキストを返す。"""
    CHAT_DIR.mkdir(parents=True, exist_ok=True)
    dest = CHAT_DIR / f"{name}.md"
    # cwd 配下なら相対パスで指示（Claudeの書き込み許可範囲に収める）
    rel = dest.relative_to(cwd) if dest.is_relative_to(cwd) else dest
    instruction = (
        f"これまでの設計対話の結論を、実装計画書として `{rel}` に"
        f"Markdownで整理して書き出して。既存があれば上書きする。"
    )
    args = ["-p", instruction, "--permission-mode", "acceptEdits"]
    if resume_id:
        args += ["--resume", resume_id]
    text, _ = _run_claude(args, cwd)
    return dest, text
