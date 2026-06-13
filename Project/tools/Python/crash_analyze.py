"""S.U.N.D.A.Y. Step8b: クラッシュの原因究明（ローカルLLM = Ollama）＋ Haikuダブルチェック

8a で出力した crash_stack.txt（シンボル付きスタック）＋ 該当ソース抜粋 ＋ ログ末尾を
Ollama(qwen2.5-coder:14b) に渡し、原因の見立て・該当箇所・対処方針を生成する（analyze_crash）。
さらに同じ材料を Claude CLI(Haiku) に独立に判定させ、Ollamaの主張を評価して3分類する（double_check）。
8c（Issue起票）がこの分析と verdict を本文に載せる。

LLM が起動していない/失敗した場合は None を返す（SUNDAY 本体は止めない）。
"""

import json
import re
import subprocess
import urllib.request
from pathlib import Path

import test_run  # REPO_DIR / PROJECT_DIR を借りる

# Ollama 設定（ai_edit.py と同じ流儀）
OLLAMA_MODEL = "qwen2.5-coder:14b"
OLLAMA_URL = "http://localhost:11434/api/generate"
OLLAMA_SYSTEM = (
    "あなたはC++/DirectX12エンジンのクラッシュ解析の専門家。"
    "鉄則: スタック最上位(#0)の『クラッシュした行』が原因の中心。まずその行が触っている"
    "ポインタ・配列・参照・イテレータを特定し、それが不正になる理由を断定する。"
    "0xC0000005(アクセス違反)なら原因は必ず『nullポインタ参照』『解放済みメモリ参照(use-after-free/ダングリング)』"
    "『配列範囲外』のいずれか——コードに即してどれかを選ぶ。"
    "禁止: 追加情報の要求 / 一般的なデバッグ手順の列挙 / 前置き / スタックに無い関数の捏造。"
    "出力は短く具体的に、日本語で。"
)

REPO_DIR = test_run.REPO_DIR

# Claude CLI（Haikuダブルチェック用。プロプラン経由＝従量課金しない）。ai_edit.py と同じ流儀。
CLAUDE_EXE = Path.home() / ".local" / "bin" / "claude.exe"
HAIKU_MODEL = "haiku"


def find_claude():
    """claude.exe の安定パスを返す。無ければ PATH 上の claude に委ねる。"""
    return str(CLAUDE_EXE) if CLAUDE_EXE.exists() else "claude"

# 例外コード → 意味（AVは特に原因カテゴリを明示してモデルを誘導する）
EXCEPTION_NAMES = {
    0xC0000005: "ACCESS_VIOLATION（メモリアクセス違反: null参照 / 解放済み参照(use-after-free) / 範囲外 のいずれか）",
    0xC00000FD: "STACK_OVERFLOW（無限再帰・深すぎる再帰）",
    0xC0000094: "INTEGER_DIVIDE_BY_ZERO（ゼロ除算）",
    0xC0000409: "STACK_BUFFER_OVERRUN / __fastfail（バッファオーバーラン等のセキュリティチェック）",
    0x80000003: "BREAKPOINT（__debugbreak / assert / 検証ブレーク）",
    0xE06D7363: "未捕捉のC++例外（throw が catch されずに終了）",
}

MAX_FRAMES = 6        # ソースを読む上位フレーム数（プロジェクト内のフレームのみ）
SNIPPET_RADIUS = 20   # 各 file:line の前後何行を抜き出すか


def _parse_stack(stack_text):
    """crash_stack.txt から (file, line) を上位順に抽出（プロジェクト内のフレームのみ）。"""
    frames = []
    repo = str(REPO_DIR).lower()
    for ln in stack_text.splitlines():
        m = re.search(r"([A-Za-z]:\\.+?):(\d+)\s*$", ln)
        if not m:
            continue
        path, line = m.group(1), int(m.group(2))
        if repo in path.lower():
            frames.append((path, line))
    return frames


def _read_snippet(path, line, radius=SNIPPET_RADIUS):
    try:
        lines = Path(path).read_text(encoding="utf-8-sig", errors="ignore").splitlines()
    except OSError:
        return None
    a = max(0, line - 1 - radius)
    b = min(len(lines), line + radius)
    out = []
    for i in range(a, b):
        mark = ">>" if (i + 1) == line else "  "
        out.append(f"{mark}{i + 1:5} {lines[i]}")
    return "\n".join(out)


def _read_line(path, line):
    """指定行のソース1行だけを返す（クラッシュ行の明示用）。"""
    try:
        lines = Path(path).read_text(encoding="utf-8-sig", errors="ignore").splitlines()
    except OSError:
        return ""
    return lines[line - 1].strip() if 0 < line <= len(lines) else ""


def _tail(path, n=20):
    try:
        return "\n".join(Path(path).read_text(encoding="utf-8", errors="ignore").splitlines()[-n:])
    except OSError:
        return ""


def _build_material(session_dir):
    """crash_stack.txt から Ollama/Haiku 共通の材料を組む。
    返り値 dict: material(本文) / loc(落ちた行 file:line) / exc_short(例外名・短)。
    crash_stack.txt が無ければ None。"""
    session_dir = Path(session_dir)
    stack_path = session_dir / "crash_stack.txt"
    if not stack_path.exists():
        print("[SUNDAY] crash_stack.txt が無いので解析をスキップ（Debugビルド/.pdb を確認）")
        return None
    stack_text = stack_path.read_text(encoding="utf-8", errors="ignore")

    frames = _parse_stack(stack_text)

    # 例外コードの意味（特に AV の原因カテゴリをモデルに渡す）
    m = re.search(r"exception_code=0x([0-9A-Fa-f]+)", stack_text)
    exc_hex = int(m.group(1), 16) if m else 0
    exc_name = EXCEPTION_NAMES.get(exc_hex, "（不明な例外コード）")
    exc_short = exc_name.split("（")[0]

    parts = [f"# 例外\n0x{exc_hex:08X} = {exc_name}"]
    parts.append("# スタックトレース（#0 が最上位＝実際に落ちた地点）\n" + stack_text.strip())

    # クラッシュ点（最上位のプロジェクトフレーム）の「落ちた行」と、その周辺ソースだけを渡す。
    # 呼び出し元フレームのソースは渡さない（弱いモデルが別フレームの目立つ行に飛びつくのを防ぐ）。
    cp_path = cp_line = None
    if frames:
        cp_path, cp_line = frames[0]
        crash_line = _read_line(cp_path, cp_line)
        parts.append(f"# 落ちた行（ここで {exc_short} が発生）\n"
                     f"{cp_path}:{cp_line}\n>> {crash_line}")
        cp_snip = _read_snippet(cp_path, cp_line, radius=12)
        if cp_snip:
            parts.append(f"# その周辺ソース（>> が落ちた行）\n{cp_snip}")
    else:
        parts.append("# 注意: プロジェクト内フレームのソースが取得できなかった。"
                     "スタック最上位の関数名と例外コードから推論せよ。")

    sess = _tail(session_dir / "session.log", 10)
    if sess.strip():
        parts.append("# session.log（末尾・参考）\n" + sess)

    loc = f"{cp_path}:{cp_line}" if cp_path else "（スタック最上位）"
    return {"material": "\n\n".join(parts), "loc": loc, "exc_short": exc_short}


def analyze_crash(session_dir):
    """セッションフォルダのクラッシュ情報を Ollama に渡し、原因分析テキストを返す。
    crash_stack.txt が無い / Ollama 不通なら None。"""
    mat = _build_material(session_dir)
    if mat is None:
        return None
    loc = mat["loc"]

    # データを先に、問いを最後に
    prompt = (
        mat["material"]
        + "\n\n---\n"
        + f"クラッシュは『落ちた行』({loc}) で起きた。他の行・他のフレームの話はするな。\n"
        + "その行が参照するポインタ/参照/反復対象(イテレータ)を1つ特定し、例外コードに照らして\n"
        + "なぜ不正になったか（null / 解放済み(use-after-free) / 範囲外）をコードに即して断定せよ。\n"
        + "重要な推論: 落ちた行が既に if(ポインタ) 等で null を弾いているのにアクセス違反するなら、\n"
        + "  null は除外される → 原因は『解放済み(use-after-free=破棄済みオブジェクトを指す非nullの残骸ポインタ)』\n"
        + "  か『範囲外』。コンテナ(highlights等)に生ポインタを貯めて要素を破棄しうる設計なら use-after-free を最有力に。\n"
        + "回答（簡潔に）:\n"
        + "1. 最有力の原因（落ちた行が何をどう不正参照したか、1〜2文で断定）\n"
        + "2. 該当箇所（file:line）\n"
        + "3. 確認/修正の方針（箇条書き2〜4点）"
    )

    body = json.dumps({
        "model": OLLAMA_MODEL,
        "system": OLLAMA_SYSTEM,
        "prompt": prompt,
        "stream": False,
        "options": {"temperature": 0.2},
    }).encode("utf-8")

    print(f"[SUNDAY] Ollama({OLLAMA_MODEL}) でクラッシュ原因を解析中...")
    req = urllib.request.Request(
        OLLAMA_URL, data=body, headers={"Content-Type": "application/json"})
    try:
        with urllib.request.urlopen(req, timeout=600) as resp:
            result = json.loads(resp.read())
        return (result.get("response") or "").strip()
    except Exception as e:
        print(f"[SUNDAY] Ollama解析に失敗（serve 起動を確認）: {e}")
        return None


VERDICTS = ("AGREE", "DISAGREE", "OLLAMA_LOCATION_WRONG")


def double_check(session_dir, ollama_analysis):
    """同じ材料を Claude CLI(Haiku) に独立判定させ、Ollamaの主張を3分類で評価する。
    返り値 dict: verdict(AGREE/DISAGREE/OLLAMA_LOCATION_WRONG/UNKNOWN) / analysis(Haiku全文)。
    crash_stack.txt が無い / CLI 失敗なら None。"""
    mat = _build_material(session_dir)
    if mat is None:
        return None

    prompt = (
        mat["material"]
        + "\n\n---\n"
        + "上記はクラッシュ時のスタックと、最上位フレーム周辺のソースだ。\n"
        + "別のローカルLLM(Ollama)が、この材料から次の分析を出した:\n"
        + "<<<OLLAMA\n" + (ollama_analysis or "(空)").strip() + "\nOLLAMA>>>\n\n"
        + f"あなたは同じ材料だけから独立に、真のクラッシュ箇所(file:line)と原因"
        + "（null参照 / 解放済み(use-after-free) / 範囲外）を判定せよ。\n"
        + "そのうえで Ollama の主張を評価する。\n"
        + "出力の1行目は必ず次のいずれか1つだけ:\n"
        + "  VERDICT: AGREE                 （Ollamaの場所・原因がともに妥当）\n"
        + "  VERDICT: DISAGREE              （クラッシュ箇所ではあるが原因/場所がOllamaと食い違う）\n"
        + "  VERDICT: OLLAMA_LOCATION_WRONG （Ollamaの指定がそもそもクラッシュ地点ですらない＝捏造）\n"
        + "2行目以降に、あなた自身が判定した file:line と短い理由（3〜5行、日本語）。"
    )

    claude = find_claude()
    print(f"[SUNDAY] Haiku({HAIKU_MODEL}) でダブルチェック中... ({claude})")
    try:
        # ファイル編集はしないので --permission-mode は付けない（テキスト回答のみ）
        proc = subprocess.run(
            [claude, "-p", prompt, "--model", HAIKU_MODEL],
            capture_output=True, text=True, encoding="utf-8", timeout=600)
    except Exception as e:
        print(f"[SUNDAY] Haikuダブルチェックに失敗（claude CLI を確認）: {e}")
        return None
    if proc.returncode != 0:
        print(f"[SUNDAY] claude CLI が異常終了 (code={proc.returncode}): {proc.stderr.strip()[:200]}")
        return None

    text = (proc.stdout or "").strip()
    verdict = "UNKNOWN"
    m = re.search(r"VERDICT:\s*(AGREE|DISAGREE|OLLAMA_LOCATION_WRONG)", text)
    if m:
        verdict = m.group(1)
    return {"verdict": verdict, "analysis": text}


# 単体テスト: python crash_analyze.py <セッションフォルダ> [--check]
#   --check を付けると Ollama分析のあと Haikuダブルチェックも走らせる。
if __name__ == "__main__":
    import sys
    if len(sys.argv) < 2:
        print("usage: python crash_analyze.py <Logs/セッションフォルダ> [--check]")
        raise SystemExit(1)
    text = analyze_crash(sys.argv[1])
    print("\n===== Ollama解析結果 =====\n" + (text or "(なし)"))
    if "--check" in sys.argv:
        chk = double_check(sys.argv[1], text)
        if chk:
            print(f"\n===== Haikuダブルチェック [VERDICT: {chk['verdict']}] =====\n"
                  + chk["analysis"])
