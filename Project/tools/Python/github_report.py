"""S.U.N.D.A.Y. Step8c: REPRODUCIBLE クラッシュを GitHub Issue に起票する。

REST API(urllib)で shun-takamura/CG2 の Issues を叩く（gh CLI は使わない）。
署名(SUNDAY-SIG: CRASH|<シーン>)で Open Issue を検索 → あればコメント追記、
無ければ新規作成（同じ不具合の重複起票を防ぐ）。
REST API は Issue に dmp を直接添付できないので、本文にローカルパス＋seed＋再現コマンドを載せる。

GITHUB_TOKEN が .env に無ければ何もしない（SUNDAY 本体は止めない）。
.env の例:  GITHUB_TOKEN=ghp_xxxxxxxx   （shun-takamura/CG2 の Issues:Read and write）
"""

import json
import os
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path

from dotenv import load_dotenv

import test_run  # REPO_DIR / GAME_EXE を借りる

REPO = "shun-takamura/CG2"     # owner/repo
API = "https://api.github.com"
REPO_DIR = test_run.REPO_DIR

load_dotenv(REPO_DIR / ".env")  # 金庫(.env)からトークンを環境変数へ


def _token():
    return os.environ.get("GITHUB_TOKEN")


def _api(method, path, token, payload=None):
    """GitHub REST API を1回叩いて JSON を返す。"""
    data = json.dumps(payload).encode("utf-8") if payload is not None else None
    req = urllib.request.Request(API + path, data=data, method=method, headers={
        "Authorization": f"Bearer {token}",
        "Accept": "application/vnd.github+json",
        "X-GitHub-Api-Version": "2022-11-28",
        "Content-Type": "application/json",
        "User-Agent": "sunday-bot",
    })
    with urllib.request.urlopen(req, timeout=30) as resp:
        raw = resp.read()
    return json.loads(raw) if raw else None


def _signature(finding):
    """重複起票を防ぐ署名。シーン単位でまとめる（同シーンの再発は同 Issue に集約）。"""
    return f"CRASH|{finding.get('scene') or '?'}"


def _stack_head(session_dir, n=14):
    """crash_stack.txt の先頭 n 行（例外＋落ちた地点付近）を要点として抜く。"""
    p = Path(session_dir) / "crash_stack.txt"
    if not p.exists():
        return "(crash_stack.txt なし)"
    return "\n".join(p.read_text(encoding="utf-8", errors="ignore").splitlines()[:n])


def _replay_cmd(finding):
    seed = finding.get("seed")
    seed_arg = f" --seed {seed}" if seed is not None else ""
    return f"{test_run.GAME_EXE.name} --replay {finding['session_dir']}{seed_arg}"


def _build_body(finding, analysis, check, sig):
    """Issue 本文（Markdown）を組む。"""
    pos = finding.get("pos")
    haiku = ("(なし)" if not check
             else f"**VERDICT: {check['verdict']}**\n\n{check['analysis']}")
    lines = [
        "## 自動検知クラッシュ（S.U.N.D.A.Y.）",
        "",
        f"- 種別: `{finding.get('type')}`（{finding.get('detail', '')}）",
        f"- シーン: `{finding.get('scene')}`",
        f"- 座標(x,y,z): `{pos}`",
        f"- frame: `{finding.get('frame')}` / HP: `{finding.get('hp')}`",
        f"- seed: `{finding.get('seed')}`",
        f"- 検知時刻: {finding.get('time')}",
        "",
        "### 再現手順（決定論リプレイで毎回再現＝REPRODUCIBLE）",
        "```",
        _replay_cmd(finding),
        "```",
        "",
        "### crash_stack.txt 要点",
        "```",
        _stack_head(finding["session_dir"]),
        "```",
        "",
        "### Ollama 分析（qwen2.5-coder:14b）",
        (analysis or "(なし)"),
        "",
        "### Haiku ダブルチェック",
        haiku,
        "",
        "### 関連ファイル（ローカル）",
        f"- dump: `{finding.get('dmp')}`",
        f"- logs: `{finding['session_dir']}`",
        "",
        f"<!-- SUNDAY-SIG: {sig} -->",
        "_この Issue は S.U.N.D.A.Y. が自動起票しました。_",
    ]
    return "\n".join(lines)


def _search_open(sig, token):
    """同じ署名を持つ Open Issue を検索し、最初の1件（dict）を返す。無ければ None。"""
    q = f'repo:{REPO} is:issue is:open "SUNDAY-SIG: {sig}"'
    path = "/search/issues?q=" + urllib.parse.quote(q)
    try:
        res = _api("GET", path, token)
    except Exception as e:
        print(f"[SUNDAY] Issue検索に失敗（続行・新規扱い）: {e}")
        return None
    items = (res or {}).get("items") or []
    return items[0] if items else None


def report_crash(finding, analysis=None, check=None):
    """REPRODUCIBLE クラッシュを Issue 起票（または既存 Issue へコメント）。
    返り値: Issue番号 / None（token無し・失敗）。"""
    token = _token()
    if not token:
        print("[SUNDAY] GITHUB_TOKEN が無いので Issue 起票をスキップ（.env を確認）")
        return None

    sig = _signature(finding)
    body = _build_body(finding, analysis, check, sig)
    try:
        existing = _search_open(sig, token)
        if existing:
            num = existing["number"]
            _api("POST", f"/repos/{REPO}/issues/{num}/comments", token,
                 {"body": "🔁 同じシーンでクラッシュを再検知:\n\n" + body})
            print(f"[SUNDAY] 既存 Issue #{num} にコメント追記: {existing.get('html_url')}")
            return num

        title = f"[SUNDAY] CRASH in {finding.get('scene') or '?'} (seed={finding.get('seed')})"
        issue = _api("POST", f"/repos/{REPO}/issues", token,
                     {"title": title, "body": body})
        print(f"[SUNDAY] Issue #{issue['number']} を起票: {issue.get('html_url')}")
        return issue["number"]
    except urllib.error.HTTPError as e:
        print(f"[SUNDAY] Issue 起票に失敗 HTTP {e.code}: {e.read().decode('utf-8', 'ignore')[:300]}")
        return None
    except Exception as e:
        print(f"[SUNDAY] Issue 起票に失敗: {e}")
        return None


def _load_from_session(session_dir):
    """anomalies.jsonl から CRASH finding / Ollama分析 / Haiku verdict を復元（単体テスト用）。"""
    p = Path(session_dir) / "anomalies.jsonl"
    finding = analysis = check = None
    if p.exists():
        for line in p.read_text(encoding="utf-8", errors="ignore").splitlines():
            try:
                rec = json.loads(line)
            except ValueError:
                continue
            t = rec.get("type")
            if t == "CRASH":
                finding = rec
            elif t == "CRASH_ANALYSIS":
                analysis = rec.get("analysis")
            elif t == "DOUBLE_CHECK":
                check = {"verdict": rec.get("verdict"), "analysis": rec.get("haiku")}
    return finding, analysis, check


# 単体テスト: python github_report.py <セッションフォルダ> [--post]
#   既定は本文を表示するだけ（dry-run）。--post を付けると実際に起票する。
if __name__ == "__main__":
    import sys
    if len(sys.argv) < 2:
        print("usage: python github_report.py <Logs/セッションフォルダ> [--post]")
        raise SystemExit(1)
    sess = sys.argv[1]
    finding, analysis, check = _load_from_session(sess)
    if finding is None:
        # anomalies.jsonl が無くても本文確認できるよう最小限の finding を作る
        finding = {"type": "CRASH", "scene": "?", "session_dir": sess,
                   "seed": None, "pos": None, "frame": None, "hp": None,
                   "dmp": str(Path(sess) / "crash.dmp"), "time": None, "detail": ""}
    if "--post" in sys.argv:
        report_crash(finding, analysis, check)
    else:
        sig = _signature(finding)
        print("===== Issue 本文プレビュー（--post で実際に起票） =====\n")
        print(_build_body(finding, analysis, check, sig))
