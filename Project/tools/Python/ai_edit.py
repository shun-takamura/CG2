"""J.A.R.V.I.S. Step3: AI編集モジュール

指示文を見て Claude CLI / Ollama に振り分け、対象ファイルを編集させる。
- Claude編集: Claude自身がファイルを読み書きする（Pythonは呼ぶだけ）
- Ollama編集: 次フェーズで実装

git_pipeline.py から run_ai_edit() を呼んで使う。単体テストも可能（末尾参照）。
"""

import subprocess
import sys
from pathlib import Path

# Claudeに振り分ける汎用プレフィックス（/prompt 入口で混在させる時の予備判定）
CLAUDE_PREFIXES = ("[C]", "クロード")

# claude.exe の安定パス（スタンドアロン版がここに入る。バージョン番号なしで固定）
CLAUDE_EXE = Path.home() / ".local" / "bin" / "claude.exe"

# claude.exe のパスを返す
def find_claude():
    if CLAUDE_EXE.exists():
        return str(CLAUDE_EXE)
    # 固定パスに無ければ PATH 上の claude に委ねる
    return "claude"

# 編集役を決める。
def route(action, instruction):
    # コマンド名で確定 → 先頭プレフィックス → 既定はOllama
    text = instruction.strip()
    if action == "EditClaude":
        return "claude", text
    if action == "Refactor":
        return "ollama", text
    for p in CLAUDE_PREFIXES:
        if text.startswith(p):
            return "claude", text[len(p):].strip()
    return "ollama", text


def edit_with_claude(instruction, cwd):
    """claude CLI を非対話で叩く。Claude自身が対象ファイルを書き換える"""
    claude = find_claude()
    print(f"Claude CLI: {claude}")

    # Claudeに編集させる
    subprocess.run(
        [claude, "-p", instruction, "--permission-mode", "acceptEdits"],
        cwd=str(cwd),
        text=True,
        check=True,
    )


def edit_with_ollama(instruction, cwd):
    raise NotImplementedError("Ollama編集は次フェーズで実装")

# 振り分けて該当AIを呼ぶ。使った役名を返す
def run_ai_edit(action, instruction, cwd):

    role, body = route(action, instruction)# 振り分け
    print(f"編集役: {role}")
    if role == "claude":
        edit_with_claude(body, cwd)# 実行
    else:
        edit_with_ollama(body, cwd)
    return role

# 直接実行のみ
if __name__ == "__main__":
    # 単体テスト用:
    #   python ai_edit.py EditClaude "[C] tools/Python/jarvis_marker.txt に挨拶コメントを1行足して"
    REPO_DIR = Path(__file__).resolve().parents[3]  # CG2 リポジトリ直下
    run_ai_edit(sys.argv[1], sys.argv[2], REPO_DIR)
