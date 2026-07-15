"""J.A.R.V.I.S. Web Console: 脱Discordの統合リモート基盤（FastAPI）

Discord(jarvis_bot.py)がやっていた devops / SUNDAY制御 / 配信を、サードパーティAPIに
依存しない自作Webコンソールへ集約する。スマホ1枚で自宅PCを完全遠隔制御するのが狙い。

構成（1プロセス・1ポート）:
  GET  /                     : 統合UI console.html（GUNDAM操作＋制御パネル）
  MOUNT /gundam -> pepper_server.app  : GUNDAMコックピット中継を丸ごと取り込み
                                        （WS /gundam/ws, メーター, Path B注入）
  POST /api/git/push         : たまった作業をGitHubへpush
  POST /api/prompt           : 指示でAI編集→ビルド→push（[C]でClaude）
  POST /api/refactor         : 規約リファクタ→ビルド→push
  POST /api/sunday/start|stop : 無人自動検証の開始/停止
  GET  /api/sunday/status    : 稼働状態＋起票イベント一覧
  GET  /api/status           : サーバ全体の状態

まだ載せない（後続フェーズ）:
  - 画面配信 WebRTC（Phase B。/api/rtc/offer。依存 aiortc/av/mss を別途通告して追加）
  - /ask /plan（claude_chat）Web化

依存: pip install fastapi uvicorn websockets （※ pepper_server と同じ本番Pythonに）
起動: python jarvis_web.py            （既定 0.0.0.0:8000, GUNDAM入力は SIM）
      python jarvis_web.py --mode B   （GUNDAM操作を vgamepad で実注入・要ViGEmBus）
注意: devops/SUNDAY APIは自宅PCを直接動かす強い操作。外部公開せずTailscale内でのみ使う。
"""

import argparse
import asyncio
import threading
import time
from pathlib import Path

from fastapi import FastAPI, Request
from fastapi.concurrency import run_in_threadpool
from fastapi.responses import HTMLResponse, JSONResponse
from dotenv import load_dotenv

# .env（GITHUB_TOKEN / OBS_WS_* 等）を環境変数へ。jarvis_bot.py と条件を揃える。
# ※ Claudeの認証は claude.exe 自身が持つ（.env とは無関係）。
load_dotenv(Path(__file__).resolve().parents[3] / ".env")

import pepper_server           # GUNDAM relay（app を /gundam に mount して再利用）
import git_pipeline            # AI編集→ビルド→push
import sunday                  # 無人ゲーム自動検証(S.U.N.D.A.Y.)

# jarvis_bot は import すると多重起動ガード(ポート占有→sys.exit)や DISCORD_TOKEN 必須が
# 副作用で走るため取り込まない。/refactor の定型指示だけ複製する（jarvis_bot.py と同一）。
REFACTOR_INSTRUCTION = "コーディング規約に従って変数名・冗長コードを整理してリファクタリングして"

DEFAULT_PORT = 8000
_CONSOLE_PATH = Path(__file__).with_name("console.html")

app = FastAPI(title="J.A.R.V.I.S. Web Console")

# GUNDAMコックピット中継を丸ごと取り込む。WS=/gundam/ws, メーター/Path Bもそのまま生きる。
app.mount("/gundam", pepper_server.app)


@app.get("/")
async def index():
    """統合UI。無ければ最小の説明ページを返す。"""
    try:
        return HTMLResponse(_CONSOLE_PATH.read_text(encoding="utf-8"))
    except FileNotFoundError:
        return HTMLResponse("<h1>J.A.R.V.I.S. Web Console</h1>"
                            "<p>console.html が見つかりません。</p>")


# ===== devops（git_pipeline を Web から。同期処理は threadpool へ逃がす） =====

@app.post("/api/git/push")
async def api_push():
    try:
        result = await run_in_threadpool(git_pipeline.push_session)
        return {"ok": True, "branch": result.get("branch")}
    except git_pipeline.PipelineError as e:
        return JSONResponse({"ok": False, "error": str(e)}, status_code=400)
    except Exception as e:
        return JSONResponse({"ok": False, "error": str(e)}, status_code=500)


async def _run_pipeline(action, instruction, target_file):
    """prompt/refactor 共通。編集→msbuild→commit を回して結果を返す。"""
    try:
        result = await run_in_threadpool(
            git_pipeline.run_pipeline, action, instruction, target_file)
        return {
            "ok": True,
            "committed": result.get("committed", False),
            "branch": result.get("branch"),
            "note": result.get("note", ""),
        }
    except git_pipeline.PipelineError as e:
        # ビルド失敗等。ログの場所も返してブラウザから辿れるように。
        log = getattr(e, "log_path", None)
        return JSONResponse({"ok": False, "error": str(e), "log_path": str(log) if log else None},
                            status_code=400)
    except Exception as e:
        return JSONResponse({"ok": False, "error": str(e)}, status_code=500)


@app.post("/api/prompt")
async def api_prompt(req: Request):
    body = await req.json()
    instruction = (body.get("instruction") or "").strip()
    target_file = (body.get("target_file") or "").strip() or None
    if not instruction:
        return JSONResponse({"ok": False, "error": "instruction が空です"}, status_code=400)
    return await _run_pipeline("Prompt", instruction, target_file)


@app.post("/api/refactor")
async def api_refactor(req: Request):
    body = await req.json()
    target_file = (body.get("target_file") or "").strip()
    if not target_file:
        return JSONResponse({"ok": False, "error": "target_file が必要です"}, status_code=400)
    return await _run_pipeline("Refactor", REFACTOR_INSTRUCTION, target_file)


# ===== S.U.N.D.A.Y.（別スレッドで1つだけ稼働。起票イベントはサーバ内に蓄積してポーリング配信） =====

_sunday_thread = None
_sunday_stop = None
_sunday_events = []          # on_issue で貯まる起票通知（ブラウザが /status で拾う）
_sunday_lock = threading.Lock()


@app.post("/api/sunday/start")
async def api_sunday_start():
    global _sunday_thread, _sunday_stop
    if _sunday_thread and _sunday_thread.is_alive():
        return JSONResponse({"ok": False, "error": "既に稼働中です"}, status_code=409)

    def on_issue(issue_no, finding):
        # 別スレッド(sunday.run)から呼ばれる。ロックして共有リストへ積むだけ（Discord送信の代わり）。
        with _sunday_lock:
            _sunday_events.append({
                "issue_no": issue_no,
                "url": f"https://github.com/shun-takamura/CG2/issues/{issue_no}",
                "scene": finding.get("scene"),
                "seed": finding.get("seed"),
                "at": time.time(),
            })

    _sunday_stop = threading.Event()
    _sunday_thread = threading.Thread(
        target=sunday.run,
        kwargs={"stop_event": _sunday_stop, "on_issue": on_issue},
        daemon=True,
    )
    _sunday_thread.start()
    return {"ok": True, "running": True}


@app.post("/api/sunday/stop")
async def api_sunday_stop():
    global _sunday_thread, _sunday_stop
    if not (_sunday_thread and _sunday_thread.is_alive()):
        return JSONResponse({"ok": False, "error": "稼働していません"}, status_code=409)
    _sunday_stop.set()
    await run_in_threadpool(_sunday_thread.join, 60.0)
    if _sunday_thread.is_alive():
        # クラッシュ解析(Ollama/Haiku)中は即停止できない
        return {"ok": True, "running": True, "pending": True}
    _sunday_thread = None
    _sunday_stop = None
    return {"ok": True, "running": False}


@app.get("/api/sunday/status")
async def api_sunday_status():
    running = bool(_sunday_thread and _sunday_thread.is_alive())
    with _sunday_lock:
        events = list(_sunday_events)
    return {"ok": True, "running": running, "events": events}


@app.get("/api/status")
async def api_status():
    running = bool(_sunday_thread and _sunday_thread.is_alive())
    return {"ok": True, "gundam_mode": pepper_server.MODE, "sunday_running": running}


def main():
    ap = argparse.ArgumentParser(description="J.A.R.V.I.S. Web Console")
    ap.add_argument("--host", default="0.0.0.0")
    ap.add_argument("--port", type=int, default=DEFAULT_PORT)
    ap.add_argument("--mode", choices=["SIM", "B"], default="SIM",
                    help="GUNDAM操作: SIM=擬似のみ / B=vgamepad実注入(要ViGEmBus)")
    args = ap.parse_args()
    # GUNDAM入力パスを確定（pepper_server 側の注入器を初期化）
    pepper_server._init_mode(args.mode)
    print(f"[JARVIS-WEB] 統合コンソール起動 http://{args.host}:{args.port}/  "
          f"GUNDAM mode={pepper_server.MODE}")
    import uvicorn
    uvicorn.run(app, host=args.host, port=args.port, log_level="info")


if __name__ == "__main__":
    main()
