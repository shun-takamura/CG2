"""J.A.R.V.I.S. Step3: AI編集モジュール

指示文を見て Claude CLI / Ollama に振り分け、対象ファイルを編集させる。
- Claude編集: Claude自身がファイルを読み書きする（Pythonは呼ぶだけ）
- Ollama編集: Ollamaは文章を返すだけなので、Python側で対象ファイルを
  読む→渡す→返答でサニタイズして上書き、まで面倒を見る

git_pipeline.py から run_ai_edit() を呼んで使う。単体テストも可能（末尾参照）。
"""

import json
import re
import subprocess
import sys
import urllib.request
from pathlib import Path

# Claudeに振り分ける汎用プレフィックス（/prompt 入口で混在させる時の予備判定）
CLAUDE_PREFIXES = ("[C]", "クロード")

# claude.exe の安定パス（スタンドアロン版がここに入る。バージョン番号なしで固定）
CLAUDE_EXE = Path.home() / ".local" / "bin" / "claude.exe"

# Ollama（ローカルLLM）設定。serve が localhost:11434 で常駐している前提
OLLAMA_MODEL = "qwen2.5-coder:14b"
OLLAMA_URL = "http://localhost:11434/api/generate"
OLLAMA_SYSTEM = (
    "あなたはC++/DirectX12のコード編集アシスタント。"
    "指示に従い、渡されたファイル全体を書き換えて返す。"
    "規約: クラス名はPascalCase、メンバ変数は末尾アンダースコア、"
    "コメントは日本語、既存のスタイルを尊重する。"
    "出力は書き換え後のファイル全文のみ。"
    "説明文・前置き・コードフェンス(```)は一切付けない。"
)

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


# LLM出力からコードフェンスを除去して中身だけ取り出す
def sanitize(text):
    text = text.strip()
    # ```lang ... ``` のブロックがあれば、その中身だけ採用
    m = re.search(r"```[a-zA-Z0-9_+-]*\n(.*?)```", text, re.DOTALL)
    if m:
        text = m.group(1)
    return text.rstrip() + "\n"  # 末尾は改行1つに正規化


def edit_with_ollama(target_file, instruction, cwd):
    """Ollamaに対象ファイル全体を書き換えさせる。
    Ollamaはファイルを触れないので、読込→送信→サニタイズ→上書き まで行う
    """
    path = Path(cwd) / target_file
    # BOMの有無を覚えておく（MSVCは日本語ソースのBOM有無に敏感なので元の流儀を保つ）
    raw = path.read_bytes()
    had_bom = raw.startswith(b"\xef\xbb\xbf")
    original = raw.decode("utf-8-sig")  # BOMがあれば自動で除去して読む

    # 指示 + 元コード をまとめてプロンプトにする
    prompt = f"{instruction}\n\n--- 対象ファイル: {target_file} ---\n{original}"
    body = json.dumps(
        {
            "model": OLLAMA_MODEL,
            "system": OLLAMA_SYSTEM,
            "prompt": prompt,
            "stream": False,
            "options": {"temperature": 0.2},  # 余計な創作を抑える
        }
    ).encode("utf-8")

    print(f"Ollama({OLLAMA_MODEL}) に編集を依頼: {target_file}")
    req = urllib.request.Request(
        OLLAMA_URL, data=body, headers={"Content-Type": "application/json"}
    )
    with urllib.request.urlopen(req, timeout=600) as resp:
        result = json.loads(resp.read())

    new_code = sanitize(result["response"])
    # 元がBOM付きだったらBOMを付けて書き戻す（流儀を維持）
    data = new_code.encode("utf-8")
    if had_bom:
        data = b"\xef\xbb\xbf" + data
    path.write_bytes(data)
    print(f"Ollama編集完了: {target_file}（{len(original)}→{len(new_code)}文字）")


# 振り分けて該当AIを呼ぶ。使った役名を返す
def run_ai_edit(action, instruction, cwd, target_file=None):
    role, body = route(action, instruction)  # 振り分け
    print(f"編集役: {role}")
    if role == "claude":
        edit_with_claude(body, cwd)  # Claudeは自分でファイルを探す
    else:
        # Ollamaは対象ファイルの指定が必須
        if not target_file:
            raise SystemExit("Ollama編集には対象ファイル(target_file)の指定が必要です")
        edit_with_ollama(target_file, body, cwd)
    return role


# 直接実行のみ
if __name__ == "__main__":
    # 単体テスト用:
    #   Claude: python ai_edit.py EditClaude "main.cppにコメントを足して"
    #   Ollama: python ai_edit.py Refactor "変数名を分かりやすく" Project/tools/Python/sample.cpp
    REPO_DIR = Path(__file__).resolve().parents[3]  # CG2 リポジトリ直下
    target = sys.argv[3] if len(sys.argv) > 3 else None
    run_ai_edit(sys.argv[1], sys.argv[2], REPO_DIR, target)
