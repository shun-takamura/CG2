"""G.U.N.D.A.M. S1: 遠隔コックピット中継サーバ（FastAPI + WebSocket）

スマホの仮想コックピット(index.html)と自宅PCの自作エンジンを繋ぐ中継点。
Discord を脱却し、サードパーティAPIに依存しない自前の操作＆計測フロントを立てる。

本ファイルは S1 の到達点：**エンジン無改修・OBS未接続でも完動する土台**。
  - GET /       : 組み込みの最小テストページ（WS疎通とメーターを単体確認）
                  ※ S2 で本番コックピット index.html に差し替え可能な構造
  - WS  /ws     : スマホ→サーバの入力JSON を受け、echo(t4確定用) と metrics を返す
  - 擬似テストモード（§9 標準搭載）: fps/encode/RTT にゆらぎノイズを合成して配信

まだ実装しない（後続ステップ）:
  - Path A: ループバックUDP→エンジン NetworkInputSource（S5/S6, USE_GUNDAM）
  - Path B: vgamepad 注入（S3, sunday.py の InputDriver 流用）
  - OBS GetStats 実測（S4, live_control.py の obsws_python 流用）
  mode(A|B) の器だけ用意し、今は擬似固定。上記が入り次第、擬似値を実測へ差し替える。

依存: pip install fastapi uvicorn
起動: python pepper_server.py           （既定 0.0.0.0:8000）
      python pepper_server.py --port 9000
確認: 同一PC/同一Tailscale内のスマホSafariで http://<PC>:8000/ を開く
"""

import argparse
import asyncio
import json
import random
import time
from pathlib import Path

from fastapi import FastAPI, WebSocket, WebSocketDisconnect
from fastapi.responses import HTMLResponse
import uvicorn

# ---- サーバ設定 ----
METRICS_HZ = 10          # metrics を push する頻度（毎秒）
DEFAULT_PORT = 8000

# 入力パス。S1では擬似固定。S3(vgamepad)実装済。S5(UDP→エンジン)で "A" 追加。
#   "A" = ループバックUDPでエンジン直結（t2/t3計測可・本命, 未実装）
#   "B" = vgamepad 注入（エンジン無改修の安全網, S3実装済）
#   "SIM" = 擬似テストモード（エンジン/OBS未接続。ゆらぎノイズを合成）
MODE = "SIM"


def _clamp(v, lo, hi):
    return lo if v < lo else hi if v > hi else v


# ---- Path B: vgamepad 注入（S3。sunday.py の InputDriver と同じ vgamepad/ViGEmBus 流儀） ----
# 未導入環境（別PC等）でも import で落ちないよう、失敗したら None にして SIM にフォールバック。
try:
    import vgamepad as _vg
except Exception:
    _vg = None


class GamepadInjector:
    """WSで受けた入力状態を仮想Xboxコントローラへそのまま反映する（Path B）。

    SUNDAY(sunday.py の InputDriver)はランダム生成だが、GUNDAM はスマホの生入力を
    ミラーするだけ。VX360Gamepad を1つ持ち reset→press→update で毎パケット同期する。"""

    def __init__(self):
        self.pad = _vg.VX360Gamepad()
        # cockpit.html のボタンビット(XInput標準値)→ XUSB_BUTTON の対応表
        B = _vg.XUSB_BUTTON
        self._map = [
            (0x0001, B.XUSB_GAMEPAD_DPAD_UP),       (0x0002, B.XUSB_GAMEPAD_DPAD_DOWN),
            (0x0004, B.XUSB_GAMEPAD_DPAD_LEFT),     (0x0008, B.XUSB_GAMEPAD_DPAD_RIGHT),
            (0x0010, B.XUSB_GAMEPAD_START),         (0x0020, B.XUSB_GAMEPAD_BACK),
            (0x0100, B.XUSB_GAMEPAD_LEFT_SHOULDER), (0x0200, B.XUSB_GAMEPAD_RIGHT_SHOULDER),
            (0x1000, B.XUSB_GAMEPAD_A),             (0x2000, B.XUSB_GAMEPAD_B),
            (0x4000, B.XUSB_GAMEPAD_X),             (0x8000, B.XUSB_GAMEPAD_Y),
        ]

    def apply(self, msg):
        btn = int(msg.get("btn", 0))
        lx = _clamp(float(msg.get("lx", 0.0)), -1.0, 1.0)
        ly = _clamp(float(msg.get("ly", 0.0)), -1.0, 1.0)  # cockpit側で上が+1。XInput左スティックYと一致
        lt = _clamp(float(msg.get("lt", 0.0)), 0.0, 1.0)
        rt = _clamp(float(msg.get("rt", 0.0)), 0.0, 1.0)
        self.pad.reset()
        for bit, xbtn in self._map:
            if btn & bit:
                self.pad.press_button(button=xbtn)
        self.pad.left_joystick_float(x_value_float=lx, y_value_float=ly)
        self.pad.left_trigger_float(value_float=lt)
        self.pad.right_trigger_float(value_float=rt)
        self.pad.update()

    def release(self):
        self.pad.reset()
        self.pad.update()


# Path B 有効時のみ生成される注入器（単一操作者前提で全WS共有）
_injector = None


def _init_mode(requested):
    """起動時にモードを確定。B指定でも vgamepad 未導入なら SIM に落として続行する。"""
    global MODE, _injector
    if requested == "B":
        if _vg is None:
            print("[GUNDAM] vgamepad 未導入のため SIM にフォールバック"
                  "（本番Pythonに pip install vgamepad + ViGEmBus ドライバが必要）")
            MODE = "SIM"
        else:
            _injector = GamepadInjector()
            MODE = "B"
            print("[GUNDAM] Path B 有効：スマホ入力を仮想Xboxコントローラへ注入します")
    else:
        MODE = "SIM"


class Advisor:
    """RTT / engine_ms / jitter / skipped から診断レベルを出す（§10）。

    S1では擬似メトリクスに対して判定する。S7で実測（engine_ms=Path A の t3-t2,
    skipped=OBS GetStats）へ差し替える。判定ロジック自体はそのまま生きる。"""

    @staticmethod
    def judge(rtt_ms, jitter_ms, skipped_pct, gpu_headroom):
        # CRITICAL: 回線が深刻に遅い or フレーム落ちが多い
        if rtt_ms >= 100.0 or skipped_pct >= 5.0:
            return ("CRITICAL",
                    "深刻な回線遅延を検知。ビットレート半減 or 配信720p化で追従性を回復。")
        # WARNING: モバイル回線特有のジッターが高い
        if 30.0 <= rtt_ms < 100.0 and jitter_ms >= 12.0:
            return ("WARNING",
                    "4Gモバイル回線特有のジッターを検知。S.U.N.D.A.Y.の無人自動デバッグへの切替を推奨。")
        # EXCELLENT: 超低遅延かつGPUに余裕
        if rtt_ms < 30.0 and gpu_headroom:
            return ("EXCELLENT",
                    "超低遅延を検知。G.U.N.D.A.M.によるフレーム単位の精密テストを推奨。")
        return ("OK", "安定動作中。")


class MetricsSynth:
    """擬似テストモード：本物っぽいゆらぎを持つメトリクスを合成する（§9）。

    エンジン/OBS が繋がるまでのスタンドイン。基準値まわりに正規ゆらぎを乗せ、
    たまにスパイク（モバイル回線のジッター再現）を混ぜる。"""

    def __init__(self):
        self._rtt = 28.0        # ネットワーク往復のベースライン(ms)
        self._fps = 144.0
        self._prev_rtt = self._rtt

    def sample(self):
        # RTT: 平均回帰＋ゆらぎ＋まれにスパイク（ジッター源）
        self._rtt += (28.0 - self._rtt) * 0.1 + random.gauss(0.0, 3.0)
        if random.random() < 0.05:
            self._rtt += random.uniform(30.0, 90.0)
        self._rtt = max(6.0, self._rtt)
        jitter = abs(self._rtt - self._prev_rtt)
        self._prev_rtt = self._rtt

        # FPS: 60〜144想定でゆらぐ。落ちるとskippedが増える相関を持たせる
        self._fps += (144.0 - self._fps) * 0.1 + random.gauss(0.0, 6.0)
        self._fps = max(30.0, min(165.0, self._fps))
        skipped = max(0.0, (120.0 - self._fps) * 0.06 + random.gauss(0.0, 0.3))

        encode_ms = max(0.5, random.gauss(3.0, 0.6))
        bitrate = max(2.0, random.gauss(12.0, 1.5))
        # engine_ms(t3-t2) は Path A でしか本来測れない。SIMでは擬似値。
        engine_ms = max(0.0, random.gauss(11.0, 2.0))
        gpu_headroom = self._fps > 100.0

        return {
            "rtt_net_ms": round(self._rtt, 1),
            "jitter_ms": round(jitter, 1),
            "engine_ms": round(engine_ms, 1),
            "fps": int(self._fps),
            "encode_ms": round(encode_ms, 1),
            "skipped_pct": round(skipped, 2),
            "bitrate_mbps": round(bitrate, 1),
            "gpu_headroom": gpu_headroom,
        }


app = FastAPI(title="G.U.N.D.A.M. cockpit relay")

# 直近の入力（メーターへタグ付けする seq を引き回すため。WS接続ごとに持つ方が素直だが
# S1は単一操作者前提で十分）
_last_seq = {"seq": 0}


# S2: 本番コックピットは同ディレクトリの cockpit.html（1枚完結）を配信。
# 毎回読むのでサーバ再起動なしにブラウザ再読込だけでUI変更が反映される（開発が楽）。
_COCKPIT_PATH = Path(__file__).with_name("cockpit.html")


@app.get("/")
async def index():
    """本番コックピット cockpit.html を返す。無ければ組込テストページにフォールバック。"""
    try:
        return HTMLResponse(_COCKPIT_PATH.read_text(encoding="utf-8"))
    except FileNotFoundError:
        return HTMLResponse(_TEST_PAGE)


@app.get("/healthz")
async def healthz():
    return {"ok": True, "mode": MODE, "metrics_hz": METRICS_HZ}


@app.websocket("/ws")
async def ws(websocket: WebSocket):
    """双方向：受信=入力JSON、送信=echo(t4確定用)＋metrics(定期push)。

    受信と送信を別タスクで回す。片方が切れたら両方畳む。"""
    await websocket.accept()
    synth = MetricsSynth()
    stop = asyncio.Event()

    async def recv_loop():
        """スマホ→サーバの入力を捌く。今は echo を即返すだけ（Path A/B はS3/S5で結線）。"""
        try:
            while not stop.is_set():
                raw = await websocket.receive_text()
                try:
                    msg = json.loads(raw)
                except json.JSONDecodeError:
                    continue
                if msg.get("t") == "in":
                    _last_seq["seq"] = msg.get("seq", _last_seq["seq"])
                    # Path B: 受けた生入力を仮想パッドへミラー（SIM時は _injector=None で素通り）
                    if _injector is not None:
                        try:
                            _injector.apply(msg)
                        except Exception:
                            pass  # 注入失敗は握りつぶす（echo/metricsは止めない）
                    # echo: スマホが t4 = performance.now() を確定して RTT_net を出すための即返し（§6）
                    await websocket.send_text(json.dumps({
                        "t": "echo", "seq": msg.get("seq"), "t1": msg.get("t1"),
                    }))
                    # TODO(S5): Path A(UDP→エンジン) 注入をここに追加（t2/t3計測）
        except WebSocketDisconnect:
            pass
        finally:
            if _injector is not None:
                _injector.release()  # 切断時に全ボタン/スティックを離す（暴走防止）
            stop.set()

    async def push_loop():
        """metrics を METRICS_HZ で push。擬似値＋アドバイザ判定を同梱（§9,§10）。"""
        interval = 1.0 / METRICS_HZ
        try:
            while not stop.is_set():
                m = synth.sample()
                level, advice = Advisor.judge(
                    m["rtt_net_ms"], m["jitter_ms"], m["skipped_pct"], m["gpu_headroom"])
                await websocket.send_text(json.dumps({
                    "t": "metrics",
                    "seq": _last_seq["seq"],
                    "mode": MODE,
                    "rtt_net_ms": m["rtt_net_ms"],
                    "engine_ms": m["engine_ms"],
                    "fps": m["fps"],
                    "encode_ms": m["encode_ms"],
                    "skipped_pct": m["skipped_pct"],
                    "bitrate_mbps": m["bitrate_mbps"],
                    "advice_level": level,
                    "advice": advice,
                }))
                await asyncio.sleep(interval)
        except WebSocketDisconnect:
            pass
        finally:
            stop.set()

    try:
        await asyncio.gather(recv_loop(), push_loop())
    except Exception:
        stop.set()


# ---- S1 動作確認用の最小テストページ（S2 で本番 index.html に差し替え） ----
_TEST_PAGE = """<!doctype html>
<html lang="ja">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1, maximum-scale=1, user-scalable=no">
<title>G.U.N.D.A.M. S1 test</title>
<style>
  :root{--bg:#0a0e17;--fg:#cfe8ff;--neon:#39a7ff;--warn:#ffcc33;--crit:#ff5566;--ok:#44dd88;}
  *{box-sizing:border-box;-webkit-tap-highlight-color:transparent;}
  body{margin:0;font-family:system-ui,-apple-system,sans-serif;background:var(--bg);color:var(--fg);
       padding:16px;user-select:none;}
  h1{font-size:16px;letter-spacing:.15em;color:var(--neon);margin:0 0 12px;}
  .card{background:#111a2b;border:1px solid #1e2f4a;border-radius:12px;padding:14px;margin-bottom:12px;}
  .meters{display:grid;grid-template-columns:1fr 1fr;gap:10px;}
  .meter{background:#0d1526;border-radius:10px;padding:10px;}
  .meter .k{font-size:11px;opacity:.6;letter-spacing:.1em;}
  .meter .v{font-size:24px;font-weight:700;font-variant-numeric:tabular-nums;}
  .pill{display:inline-block;padding:3px 10px;border-radius:999px;font-size:12px;font-weight:700;}
  .EXCELLENT,.OK{background:rgba(68,221,136,.15);color:var(--ok);}
  .WARNING{background:rgba(255,204,51,.15);color:var(--warn);}
  .CRITICAL{background:rgba(255,85,102,.18);color:var(--crit);}
  .advice{font-size:13px;line-height:1.5;margin-top:8px;min-height:2.4em;}
  button{width:100%;padding:14px;border:0;border-radius:10px;background:var(--neon);color:#031326;
         font-size:15px;font-weight:700;letter-spacing:.05em;}
  .stat{font-size:11px;opacity:.55;margin-top:6px;}
</style>
</head>
<body>
  <h1>G.U.N.D.A.M. ▸ S1 RELAY TEST</h1>

  <div class="card">
    <span id="conn" class="pill CRITICAL">DISCONNECTED</span>
    <span id="mode" class="stat"></span>
  </div>

  <div class="card">
    <div class="meters">
      <div class="meter"><div class="k">RTT_NET</div><div class="v"><span id="rtt">--</span> ms</div></div>
      <div class="meter"><div class="k">ENGINE</div><div class="v"><span id="eng">--</span> ms</div></div>
      <div class="meter"><div class="k">FPS</div><div class="v"><span id="fps">--</span></div></div>
      <div class="meter"><div class="k">ENCODE</div><div class="v"><span id="enc">--</span> ms</div></div>
      <div class="meter"><div class="k">SKIPPED</div><div class="v"><span id="skp">--</span> %</div></div>
      <div class="meter"><div class="k">BITRATE</div><div class="v"><span id="bit">--</span> M</div></div>
    </div>
    <div class="stat">RTT_echo(スマホ単一時計) : <span id="rttecho">--</span> ms</div>
  </div>

  <div class="card">
    <span id="lvl" class="pill OK">--</span>
    <div id="advice" class="advice"></div>
  </div>

  <button id="send">TEST INPUT 送信 (echo で t4 確定)</button>

<script>
  let ws, seq = 0;
  const $ = id => document.getElementById(id);

  function connect(){
    ws = new WebSocket("ws://" + location.host + "/ws");
    ws.onopen = () => { $("conn").textContent = "CONNECTED"; $("conn").className = "pill OK"; };
    ws.onclose = () => {
      $("conn").textContent = "DISCONNECTED"; $("conn").className = "pill CRITICAL";
      setTimeout(connect, 1000);           // 指数バックオフの簡易版（S9で本実装）
    };
    ws.onmessage = ev => {
      const m = JSON.parse(ev.data);
      if(m.t === "metrics"){
        $("rtt").textContent = m.rtt_net_ms;
        $("eng").textContent = m.engine_ms;
        $("fps").textContent = m.fps;
        $("enc").textContent = m.encode_ms;
        $("skp").textContent = m.skipped_pct;
        $("bit").textContent = m.bitrate_mbps;
        $("mode").textContent = "mode=" + m.mode;
        $("lvl").textContent = m.advice_level;
        $("lvl").className = "pill " + m.advice_level;
        $("advice").textContent = m.advice;
      } else if(m.t === "echo"){
        // スマホ単一時計で RTT_net = t4 - t1（クロックドメインを跨がない §6）
        const rtt = performance.now() - m.t1;
        $("rttecho").textContent = rtt.toFixed(1);
      }
    };
  }
  connect();

  $("send").addEventListener("click", () => {
    if(!ws || ws.readyState !== 1) return;
    seq += 1;
    ws.send(JSON.stringify({ t:"in", seq, t1: performance.now(),
                             btn:0, lx:0, ly:0, lt:0, rt:0 }));
  });
</script>
</body>
</html>
"""


def main():
    ap = argparse.ArgumentParser(description="G.U.N.D.A.M. cockpit relay (S1)")
    ap.add_argument("--host", default="0.0.0.0",
                    help="バインドホスト（Tailscale内で使うなら0.0.0.0のまま）")
    ap.add_argument("--port", type=int, default=DEFAULT_PORT)
    ap.add_argument("--mode", choices=["SIM", "B"], default="SIM",
                    help="SIM=擬似のみ / B=vgamepad注入(要ViGEmBus)。A(エンジン直結)はS5で追加")
    args = ap.parse_args()
    _init_mode(args.mode)
    print(f"[GUNDAM] relay起動 http://{args.host}:{args.port}/  mode={MODE}")
    uvicorn.run(app, host=args.host, port=args.port, log_level="info")


if __name__ == "__main__":
    main()
