"""S.U.N.D.A.Y. Step6: ランダム入力 → ログ追尾 → 異常検知

無人でゲームを起動し、STAGEPLAY をランダム操作で遊び倒しながら
エンジンが書く各ログ（event.log / state.log）をリアルタイム追尾して異常を検知する。

検知できたら anomalies.jsonl に記録し、コンソールに通知する（v1はここまで）。
再現性判定(Step7)・GitHub Issue起票(Step8)・JARVIS統合(Step9)は次段で追加する。

前提:
- Debug ビルドの CG2_0_1.exe（全カテゴリ TRACE 出力。state/event/input.log が出る）
- 入力は仮想Xboxコントローラ(vgamepad/ViGEmBus)で送る。XInput はウィンドウの
  フォーカスやマウス位置に依存しないため、無人稼働に向く。
- シーン認識は event.log の SCENE_CHANGE を追尾して行う（Title/Hub は state.log を出さないため）

依存: pip install vgamepad（初回は ViGEmBus ドライバのインストールを促される）
単体実行: python sunday.py  （Ctrl+C で停止）
"""

import json
import math
import random
import subprocess
import time
from datetime import datetime
from pathlib import Path

import pygetwindow as gw
import vgamepad as vg  # 仮想Xboxコントローラ（要 ViGEmBus ドライバ + pip install vgamepad）

import test_run  # GAME_EXE / PROJECT_DIR / WINDOW_TITLE を借りる
import crash_analyze  # Step8b: Ollama原因究明 + Haikuダブルチェック
import github_report  # Step8c: REPRODUCIBLE クラッシュを GitHub Issue 起票

# ---- 調整パラメータ（しきい値は環境に合わせて触る） ----
LOGS_DIR = test_run.PROJECT_DIR / "Logs"

HANG_SEC = 3.0          # STAGEPLAY中に state.log がこの秒数更新されない → ハング
STUCK_SEC = 2.0         # 移動入力中に座標がこの秒数ほぼ不変 → スタック候補
STUCK_EPS = 0.05        # 「ほぼ不変」とみなす座標移動量(ワールド単位)
STUCK_COOLDOWN = 5.0    # 同じスタックを連発通知しない間隔
COORD_LIMIT = 100000.0  # |x|,|y|,|z| がこれを超えたら範囲外
SESSION_MAX_SEC = 150.0 # 1セッションの最大時間（約2.5分）。超えたら一旦リスタート
NAV_INTERVAL = 0.7      # Title/Hub/Result で Enter を送る間隔

# 起動待ち: Debugビルドは起動が重く最初の数秒は frame=0 のまま。
# state.log の frame がここまで進んだら「ゲームが本当に動き出した」とみなして検知を始める。
READY_FRAMES = 30       # この frame に到達したら稼働中と判定
STARTUP_TIMEOUT = 60.0  # この秒数内に動き出さなければ起動失敗として扱う

# 再現性判定（CRASH検知時）: --replay でM回再生し、再現するか確認
REPLAY_RUNS = 3
REPLAY_TIMEOUT = 180.0  # 1回の再生がこの秒数を超えたら打ち切り（記録分を再生し終える想定）

# STAGEPLAY中のランダム入力。仮想Xboxコントローラ(vgamepad)で送るため
# ウィンドウのフォーカス/マウス位置に依存しない（XInputはフォーカス無関係に読まれる）。
# キーコンフィグの gp.* バインドに対応：
#   左スティック=移動 / A=Dodge / B=MeleeWeak / X=Heal / Y=MeleeStrong / LB=LockCam / RT=Fire
# Pause(Start) は誤終了防止で除外。
MOVE_WEIGHT = 0.75      # 各決定で移動を選ぶ確率（残りがアクション/射撃）
FIRE_WEIGHT = 0.12      # 残りのうち射撃(RT)を選ぶ割合
DECISION_INTERVAL = 0.15
MOVE_HOLD_RANGE = (0.25, 0.9)   # 左スティックを倒し続ける時間
ACTION_HOLD = 0.10              # ボタン/トリガーの単発押下時間

ACTION_BUTTONS = [
    vg.XUSB_BUTTON.XUSB_GAMEPAD_A,              # Dodge
    vg.XUSB_BUTTON.XUSB_GAMEPAD_B,              # MeleeWeak
    vg.XUSB_BUTTON.XUSB_GAMEPAD_X,              # Heal
    vg.XUSB_BUTTON.XUSB_GAMEPAD_Y,              # MeleeStrong
    vg.XUSB_BUTTON.XUSB_GAMEPAD_LEFT_SHOULDER,  # LockCam
]
BTN_CONFIRM = vg.XUSB_BUTTON.XUSB_GAMEPAD_A     # Title/Hub の決定（MenuConfirm=gp.A）


def find_session_dir(after_ts, timeout=15.0):
    """起動時刻 after_ts 以降に作られた最新のセッションフォルダを待って返す。"""
    deadline = time.time() + timeout
    while time.time() < deadline:
        if LOGS_DIR.exists():
            cands = [d for d in LOGS_DIR.iterdir()
                     if d.is_dir() and d.stat().st_mtime >= after_ts - 1.0]
            if cands:
                return max(cands, key=lambda d: d.stat().st_mtime)
        time.sleep(0.3)
    return None


def read_seed(session_dir):
    """session.log から記録シードを拾う（Issue/再現用）。無ければ None。"""
    p = session_dir / "session.log"
    if not p.exists():
        return None
    for line in p.read_text(encoding="utf-8", errors="ignore").splitlines():
        i = line.find("seed=")
        if i != -1:
            tok = line[i + 5:].split()[0]
            try:
                return int(tok)
            except ValueError:
                return None
    return None


def focus_game():
    """ゲームウィンドウを前面に出す（描画を見えるように。コントローラ入力自体はフォーカス不要）。"""
    wins = gw.getWindowsWithTitle(test_run.WINDOW_TITLE)
    if not wins:
        return None
    win = wins[0]
    try:
        if win.isMinimized:
            win.restore()
        win.activate()
    except Exception:
        pass
    return win


class Tailer:
    """ログファイルを末尾追尾し、確定した新規行だけを返す。"""

    def __init__(self, path):
        self.path = Path(path)
        self.offset = 0
        self._buf = ""

    def poll(self):
        if not self.path.exists():
            return []
        lines = []
        with self.path.open("r", encoding="utf-8", errors="ignore") as f:
            f.seek(self.offset)
            data = f.read()
            self.offset = f.tell()
        self._buf += data
        while "\n" in self._buf:
            line, self._buf = self._buf.split("\n", 1)
            line = line.rstrip("\r")
            if line:
                lines.append(line)
        return lines


def _kv(line):
    """'... key=val key=val' を dict にする（プレフィックスは無視）。"""
    out = {}
    for tok in line.split():
        if "=" in tok:
            k, v = tok.split("=", 1)
            out[k] = v
    return out


def parse_state(line):
    """state.log 行 → (frame, x, y, z, hp_cur, hp_max, scene) or None。"""
    if "frame=" not in line or "scene=" not in line:
        return None
    d = _kv(line)
    try:
        # 座標は nan/inf を文字列で受けうるので float() で素直に変換（失敗=異常値扱い）
        x = float(d.get("x", "nan"))
        y = float(d.get("y", "nan"))
        z = float(d.get("z", "nan"))
    except ValueError:
        x = y = z = float("nan")
    hp = d.get("hp", "0/0").split("/")
    return {
        "frame": int(d.get("frame", "0")),
        "x": x, "y": y, "z": z,
        "hp_cur": int(hp[0]) if hp[0].lstrip("-").isdigit() else 0,
        "scene": d.get("scene", "?"),
        "raw": line,
    }


def event_kind(line):
    """event.log 行から種別を判定。"""
    for kind in ("SCENE_CHANGE", "MOVE_BLOCKED", "GAMEOVER", "DEATH"):
        if kind in line:
            return kind
    return None


class InputDriver:
    """仮想Xboxコントローラ(vgamepad)で入力を送る。ウィンドウのフォーカスに依存しない。
    STAGEPLAYでランダム操作、他シーンは A ボタンで前進。"""

    def __init__(self):
        self.pad = vg.VX360Gamepad()
        self.btn_until = {}             # XUSB_BUTTON -> release_time
        self.move_vec = (0.0, 0.0)      # 左スティック方向
        self.move_until = 0.0           # 左スティックを倒し続ける期限
        self.rt_until = 0.0             # 右トリガー(Fire)の期限
        self.last_decision = 0.0
        self.last_nav = 0.0
        self.move_active_since = None   # 移動が始まった時刻（スタック判定用）

    def _send(self, now):
        # 期限切れを解除
        self.btn_until = {b: t for b, t in self.btn_until.items() if now < t}
        if now >= self.move_until:
            self.move_vec = (0.0, 0.0)
            self.move_active_since = None
        rt = 1.0 if now < self.rt_until else 0.0
        # 現在状態を仮想パッドに反映して送出
        self.pad.reset()
        for b in self.btn_until:
            self.pad.press_button(button=b)
        self.pad.left_joystick_float(x_value_float=self.move_vec[0], y_value_float=self.move_vec[1])
        self.pad.right_trigger_float(value_float=rt)
        self.pad.update()

    def is_moving(self):
        return time.time() < self.move_until

    def tick_stageplay(self, now):
        if now - self.last_decision >= DECISION_INTERVAL:
            self.last_decision = now
            r = random.random()
            if r < MOVE_WEIGHT:
                ang = random.uniform(0.0, 2.0 * math.pi)
                self.move_vec = (math.cos(ang), math.sin(ang))
                self.move_until = now + random.uniform(*MOVE_HOLD_RANGE)
                if self.move_active_since is None:
                    self.move_active_since = now
            elif r < MOVE_WEIGHT + FIRE_WEIGHT:
                self.rt_until = now + ACTION_HOLD            # Fire（右トリガー）
            else:
                self.btn_until[random.choice(ACTION_BUTTONS)] = now + ACTION_HOLD  # 単発アクション
        self._send(now)

    def tick_nav(self, now):
        """Title/Hub/Result：A(決定)だけ送って前進（DPadは送らず誤終了防止）。"""
        self.move_vec = (0.0, 0.0)
        self.move_until = 0.0
        self.rt_until = 0.0
        if now - self.last_nav >= NAV_INTERVAL:
            self.last_nav = now
            self.btn_until[BTN_CONFIRM] = now + ACTION_HOLD
        self._send(now)

    def release_all(self):
        self.btn_until.clear()
        self.move_vec = (0.0, 0.0)
        self.move_until = 0.0
        self.rt_until = 0.0
        self.move_active_since = None
        self.pad.reset()
        self.pad.update()


class SundaySession:
    """1回のゲーム起動ぶんの監視。異常を検知したら finding を返す。"""

    def __init__(self, proc, session_dir):
        self.proc = proc
        self.session_dir = session_dir
        self.seed = read_seed(session_dir)
        self.ev_tail = Tailer(session_dir / "event.log")
        self.st_tail = Tailer(session_dir / "state.log")
        self.scene = None
        self.last_state = None
        self.last_state_wall = time.time()
        self.last_move_blocked_wall = 0.0
        self.max_frame = -1             # これまでに見た最大 frame（起動判定用）
        # スタック判定用の基準
        self.stuck_anchor = None        # (x, y, z)
        self.stuck_anchor_wall = 0.0
        self.stuck_last_report = 0.0

    def _record_event(self, line):
        kind = event_kind(line)
        if kind == "SCENE_CHANGE":
            d = _kv(line)
            self.scene = d.get("to", self.scene)
        elif kind == "MOVE_BLOCKED":
            self.last_move_blocked_wall = time.time()
        return kind

    def _record_state(self, line):
        st = parse_state(line)
        if st:
            self.last_state = st
            self.last_state_wall = time.time()
            if st["frame"] > self.max_frame:
                self.max_frame = st["frame"]
        return st

    def detect(self, driver):
        """異常があれば finding(dict) を返す。無ければ None。"""
        now = time.time()

        # --- ハード: クラッシュ（プロセス異常終了） ---
        if self.proc.poll() is not None:
            dmp = self.session_dir / "crash.dmp"
            return self._finding(
                "CRASH",
                detail=f"process exited code={self.proc.returncode}",
                dmp=str(dmp) if dmp.exists() else None,
            )

        # STAGEPLAY 以外は時間ベース判定をしない（state.log が出ないため）
        if self.scene != "STAGEPLAY" or self.last_state is None:
            return None

        x, y, z = self.last_state["x"], self.last_state["y"], self.last_state["z"]

        # --- ハード: 座標 NaN / Inf ---
        if not all(math.isfinite(v) for v in (x, y, z)):
            return self._finding("COORD_NAN", detail=f"x={x} y={y} z={z}")

        # --- ハード: 座標 範囲外 ---
        if max(abs(x), abs(y), abs(z)) > COORD_LIMIT:
            return self._finding("COORD_OOR", detail=f"x={x} y={y} z={z}")

        # --- ハード: ハング（state.log が更新されない） ---
        if now - self.last_state_wall > HANG_SEC:
            return self._finding(
                "HANG", detail=f"no state update for {now - self.last_state_wall:.1f}s")

        # --- ソフト: スタック（移動中なのに座標不変 かつ MOVE_BLOCKED 無し） ---
        if driver.is_moving():
            if self.stuck_anchor is None:
                self.stuck_anchor = (x, y, z)
                self.stuck_anchor_wall = now
            else:
                moved = math.dist(self.stuck_anchor, (x, y, z))
                if moved > STUCK_EPS:
                    # 動いた → アンカー更新
                    self.stuck_anchor = (x, y, z)
                    self.stuck_anchor_wall = now
                elif now - self.stuck_anchor_wall > STUCK_SEC:
                    # 一定時間ほぼ不変。直近に MOVE_BLOCKED が無ければスタック候補
                    # （同じスタックを毎ループ通知しないようクールダウンを挟む）
                    if (now - self.last_move_blocked_wall > STUCK_SEC
                            and now - self.stuck_last_report > STUCK_COOLDOWN):
                        self.stuck_last_report = now
                        return self._finding(
                            "STUCK",
                            detail=f"moving but coords frozen {STUCK_SEC}s, no MOVE_BLOCKED")
        else:
            self.stuck_anchor = None

        return None

    def _finding(self, kind, detail="", dmp=None):
        st = self.last_state or {}
        return {
            "time": datetime.now().isoformat(timespec="seconds"),
            "type": kind,
            "detail": detail,
            "scene": self.scene,
            "frame": st.get("frame"),
            "pos": [st.get("x"), st.get("y"), st.get("z")],
            "hp": st.get("hp_cur"),
            "seed": self.seed,
            "session_dir": str(self.session_dir),
            "dmp": dmp,
        }

    def pump_logs(self):
        for line in self.ev_tail.poll():
            self._record_event(line)
        for line in self.st_tail.poll():
            self._record_state(line)


def report_anomaly(finding):
    """anomalies.jsonl に追記し、コンソールへ通知。"""
    out = Path(finding["session_dir"]) / "anomalies.jsonl"
    with out.open("a", encoding="utf-8") as f:
        f.write(json.dumps(finding, ensure_ascii=False) + "\n")
    print("\n" + "=" * 60)
    print(f"[SUNDAY] 異常検知: {finding['type']}  ({finding['detail']})")
    print(f"  scene={finding['scene']} frame={finding['frame']} pos={finding['pos']}")
    print(f"  seed={finding['seed']}  session={finding['session_dir']}")
    if finding["dmp"]:
        print(f"  dump={finding['dmp']}")
    print("=" * 60 + "\n")


def reproduce_crash(orig_session_dir):
    """記録セッションを --replay で REPLAY_RUNS 回再生し、クラッシュが再現するか判定。
    決定論リプレイ（同一シード＋dt列＋入力）なので、ロジック起因なら毎回再現する。
    (reproduced_count, total) を返す。"""
    print(f"[SUNDAY] 再現性判定: {orig_session_dir.name} を {REPLAY_RUNS} 回再生して確認...")
    reproduced = 0
    for i in range(REPLAY_RUNS):
        launch_ts = time.time()
        proc = subprocess.Popen(
            [str(test_run.GAME_EXE), "--replay", str(orig_session_dir)],
            cwd=str(test_run.PROJECT_DIR))
        replay_dir = find_session_dir(launch_ts)

        # プロセス終了まで待つ（再生し終える=正常終了 / クラッシュ=異常終了+crash.dmp）
        deadline = time.time() + REPLAY_TIMEOUT
        while proc.poll() is None and time.time() < deadline:
            time.sleep(0.2)

        crashed = False
        if proc.poll() is None:
            proc.terminate()  # タイムアウト（再現せずに走り切れなかった）
        elif replay_dir and (replay_dir / "crash.dmp").exists():
            crashed = True
        elif proc.returncode not in (0, None):
            crashed = True  # dmp は無いが異常終了

        reproduced += 1 if crashed else 0
        tag = "再現(CRASH)" if crashed else "再現せず"
        print(f"  [{i + 1}/{REPLAY_RUNS}] {tag}"
              + (f"  ({replay_dir.name})" if replay_dir else ""))
        time.sleep(0.5)
    return reproduced, REPLAY_RUNS


def report_reproduce(finding, result):
    """再現性判定の結果を anomalies.jsonl に追記し通知。"""
    reproduced, total = result
    if reproduced == total:
        verdict = "REPRODUCIBLE"     # 毎回再現＝再現性ありバグ（高優先）
    elif reproduced > 0:
        verdict = "FLAKY"            # たまに再現
    else:
        verdict = "NOT_REPRODUCED"   # 環境・タイミング依存

    rec = {
        "time": datetime.now().isoformat(timespec="seconds"),
        "type": "REPRODUCE_RESULT",
        "of": finding["type"],
        "verdict": verdict,
        "reproduced": reproduced,
        "runs": total,
        "seed": finding["seed"],
        "session_dir": finding["session_dir"],
    }
    out = Path(finding["session_dir"]) / "anomalies.jsonl"
    with out.open("a", encoding="utf-8") as f:
        f.write(json.dumps(rec, ensure_ascii=False) + "\n")
    print("\n" + "#" * 60)
    print(f"[SUNDAY] 再現性判定: {verdict}  ({reproduced}/{total} 回再現)")
    print(f"  seed={finding['seed']}  session={finding['session_dir']}")
    print("#" * 60 + "\n")
    return verdict


def save_analysis(finding, analysis):
    """Ollama のクラッシュ原因分析を anomalies.jsonl に追記し、コンソールへ表示。"""
    rec = {
        "time": datetime.now().isoformat(timespec="seconds"),
        "type": "CRASH_ANALYSIS",
        "session_dir": finding["session_dir"],
        "seed": finding["seed"],
        "analysis": analysis,
    }
    out = Path(finding["session_dir"]) / "anomalies.jsonl"
    with out.open("a", encoding="utf-8") as f:
        f.write(json.dumps(rec, ensure_ascii=False) + "\n")
    print("\n----- クラッシュ原因究明 (Ollama) -----")
    print(analysis)
    print("--------------------------------------\n")


def save_double_check(finding, ollama_analysis, check):
    """Haikuダブルチェックの結果(verdict+両者の分析)を anomalies.jsonl に追記し表示。"""
    rec = {
        "time": datetime.now().isoformat(timespec="seconds"),
        "type": "DOUBLE_CHECK",
        "session_dir": finding["session_dir"],
        "seed": finding["seed"],
        "verdict": check["verdict"],     # AGREE / DISAGREE / OLLAMA_LOCATION_WRONG / UNKNOWN
        "ollama": ollama_analysis,
        "haiku": check["analysis"],
    }
    out = Path(finding["session_dir"]) / "anomalies.jsonl"
    with out.open("a", encoding="utf-8") as f:
        f.write(json.dumps(rec, ensure_ascii=False) + "\n")
    print("\n----- ダブルチェック (Haiku) -----")
    print(f"VERDICT: {check['verdict']}")
    print(check["analysis"])
    print("----------------------------------\n")


def run_session():
    """ゲームを1回起動し、終了/異常まで監視する。finding or None を返す。"""
    launch_ts = time.time()
    proc = subprocess.Popen([str(test_run.GAME_EXE)], cwd=str(test_run.PROJECT_DIR))

    session_dir = find_session_dir(launch_ts)
    if session_dir is None:
        proc.terminate()
        raise RuntimeError("セッションフォルダが見つかりません（Logs/ 生成を確認）")
    print(f"[SUNDAY] session: {session_dir.name}")

    sess = SundaySession(proc, session_dir)
    driver = InputDriver()

    # --- 起動待ち: state.log の frame が READY_FRAMES まで進む＝ゲームが本当に動き出すまで待つ ---
    # Debugビルドの重い起動中は frame=0 のまま。ここで検知を始めると HANG/STUCK の誤判定になる。
    deadline = time.time() + STARTUP_TIMEOUT
    last_focus = 0.0
    while True:
        now = time.time()
        sess.pump_logs()
        if proc.poll() is not None:
            dmp = session_dir / "crash.dmp"
            finding = sess._finding(
                "CRASH", detail=f"exited during startup code={proc.returncode}",
                dmp=str(dmp) if dmp.exists() else None)
            report_anomaly(finding)
            return finding
        if sess.max_frame >= READY_FRAMES:
            break
        if now > deadline:
            finding = sess._finding(
                "STARTUP_TIMEOUT",
                detail=f"frame {READY_FRAMES} に {STARTUP_TIMEOUT}s 以内に到達せず (max_frame={sess.max_frame})")
            report_anomaly(finding)
            return finding
        if now - last_focus > 2.0:
            focus_game()  # 起動直後に前面化（入力送出先を確保）
            last_focus = now
        time.sleep(0.05)

    print(f"[SUNDAY] ready (frame={sess.max_frame})")
    focus_game()
    sess.last_state_wall = time.time()  # 稼働開始を基準に検知タイマーをリセット
    start = time.time()
    last_focus = start

    try:
        while True:
            now = time.time()
            sess.pump_logs()

            finding = sess.detect(driver)
            if finding:
                report_anomaly(finding)
                # ハード異常はプロセスが死んでいる/不安定なのでセッション終了
                if finding["type"] in ("CRASH", "HANG", "COORD_NAN", "COORD_OOR"):
                    return finding
                # ソフト異常(STUCK)は記録して継続観察

            # 入力（前面維持は数秒おき）
            if now - last_focus > 5.0:
                focus_game()
                last_focus = now
            if sess.scene == "STAGEPLAY":
                driver.tick_stageplay(now)
            else:
                driver.tick_nav(now)

            # セッション上限：一旦閉じてリスタート（呼び出し側で再起動）
            if now - start > SESSION_MAX_SEC:
                print("[SUNDAY] セッション上限。リスタートします。")
                return None

            time.sleep(0.03)
    finally:
        driver.release_all()
        if proc.poll() is None:
            proc.terminate()


def main():
    print("[SUNDAY] 起動。Ctrl+C で停止。")
    try:
        while True:
            finding = run_session()
            # CRASH を検知したら、同じ記録を再生して再現性を判定する
            if finding and finding.get("type") == "CRASH":
                result = reproduce_crash(Path(finding["session_dir"]))
                verdict = report_reproduce(finding, result)
                # 再現性ありのクラッシュ: Ollamaで原因究明 → Haikuでダブルチェック → Issue起票
                if verdict == "REPRODUCIBLE":
                    analysis = crash_analyze.analyze_crash(finding["session_dir"])
                    check = None
                    if analysis:
                        save_analysis(finding, analysis)
                        check = crash_analyze.double_check(finding["session_dir"], analysis)
                        if check:
                            save_double_check(finding, analysis, check)
                    # GitHub Issue 起票（GITHUB_TOKEN が無ければ内部でスキップ）
                    github_report.report_crash(finding, analysis, check)
            time.sleep(1.0)  # 次セッションまで小休止
    except KeyboardInterrupt:
        print("\n[SUNDAY] 停止しました。")


if __name__ == "__main__":
    main()
