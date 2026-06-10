"""J.A.R.V.I.S. Step5: ライブ配信制御

ゲームを起動 → OBS仮想カメラ開始 → Discordカメラ ON（ホットキー）で配信開始。
逆順で配信停止。状態（ゲームのプロセス／配信中フラグ）をコマンドを跨いで保持する。

配信の実体は「OBS仮想カメラをDiscordのカメラとして映す」方式。
全工程が公式API・プロセス制御・グローバルホットキーで、画面のクリックは一切しない。
"""

import ctypes
import os
import subprocess
import time
from pathlib import Path

import cv2
import mss
import numpy as np
import pyautogui
import pydirectinput
import pygetwindow as gw
import obsws_python as obs

import test_run  # GAME_EXE / PROJECT_DIR を借りる（importしてもゲームは起動しない）

# OBS接続情報。ポート/パスワードは「接続する瞬間」に読む
# （モジュール読込時は .env がまだ未ロードのことがあるため、ここで定数化しない）
OBS_HOST = "localhost"

# 「カメラをオン」ボタンのスクショ（画像認識でクリックするため。後で1枚用意する）
CAM_ON_IMG = Path(__file__).resolve().parent / "cam_on_button.png"

# 高DPI/拡大率の違うモニターでも、スクショと座標が縮小されないようにする
try:
    ctypes.windll.shcore.SetProcessDpiAwareness(2)  # PER_MONITOR_AWARE
except Exception:
    try:
        ctypes.windll.user32.SetProcessDPIAware()
    except Exception:
        pass

# Discordのキー割り当てと一致させるホットキー（修飾キー, 主キー）
CAM_HOTKEY = (["ctrl", "shift"], "f10")         # カメラのオンオフ
VC_HOTKEY = (["ctrl", "shift"], "f11")          # J.A.R.V.I.S.のVCに参加/切替
DISCONNECT_HOTKEY = (["ctrl", "shift"], "f12")  # VCから切断

# 起動からOBS捕捉が安定するまでの待機秒数
WARMUP_SEC = 5

# コマンドを跨いで覚えておく状態（このモジュールが生きている間だけ保持）
_state = {"game": None, "live": False}


# グローバルホットキーを送る（修飾キーを押しっぱなしで主キーを叩く）
def _send_hotkey(combo):
    mods, key = combo
    for m in mods:
        pydirectinput.keyDown(m)
    pydirectinput.press(key)
    for m in reversed(mods):
        pydirectinput.keyUp(m)


# Discordのメインウィンドウを取得する
def _get_discord_window():
    for w in gw.getWindowsWithTitle("Discord"):
        if "Discord" in w.title:
            return w
    return None


# 全モニターを撮影してテンプレート画像を探す（サブモニター対応）
# 見つかれば実スクリーン座標の中心を返す。pyautogui標準と違い全画面を見られる
def _locate_on_all_screens(template_path, confidence=0.8):
    template = cv2.imread(str(template_path), cv2.IMREAD_COLOR)
    if template is None:
        print(f"テンプレート画像が読めません: {template_path}")
        return None  # 画像ファイルが無い/読めない
    th, tw = template.shape[:2]

    # モニターを1枚ずつ個別に撮影して探す。
    # 各モニターの原点(left/top)はクリックAPIと同じ座標系なので、
    # 拡大率違い（150%/100%）やオフセット配置でもズレない
    # （全画面まとめ撮りだと物理レイアウトと座標系がズレてクリック位置が狂う）
    with mss.mss() as sct:
        for mon in sct.monitors[1:]:  # [0]は全結合なので除外、[1:]が各モニター
            shot = cv2.cvtColor(np.array(sct.grab(mon)), cv2.COLOR_BGRA2BGR)
            gh, gw = shot.shape[:2]  # 撮影した実ピクセルのサイズ
            res = cv2.matchTemplate(shot, template, cv2.TM_CCOEFF_NORMED)
            _, max_val, _, max_loc = cv2.minMaxLoc(res)
            print(
                f"モニター(枠{mon['left']},{mon['top']} {mon['width']}x{mon['height']} "
                f"撮影{gw}x{gh}) 一致度: {max_val:.3f}（しきい値 {confidence}）"
            )
            if max_val >= confidence:
                # 撮影ピクセル座標 → クリック座標系へ換算（拡大率の違いを倍率で吸収）
                sx = mon["width"] / gw
                sy = mon["height"] / gh
                cx = mon["left"] + (max_loc[0] + tw // 2) * sx
                cy = mon["top"] + (max_loc[1] + th // 2) * sy
                return (cx, cy)
    return None


# 指定の実スクリーン座標を直接クリック（Windows APIを叩く＝複数モニター/高DPIでもズレない）
def _click_at(x, y):
    user32 = ctypes.windll.user32
    user32.SetCursorPos(int(x), int(y))
    time.sleep(0.05)
    user32.mouse_event(0x0002, 0, 0, 0, 0)  # 左ボタン押下
    time.sleep(0.02)
    user32.mouse_event(0x0004, 0, 0, 0, 0)  # 左ボタン解放


# 「カメラをオン」ボタンを探してクリック（モーダル描画を待ちつつ数回試す）
def _click_camera_on():
    for _ in range(10):
        pos = _locate_on_all_screens(CAM_ON_IMG, confidence=0.8)
        if pos:
            _click_at(pos[0], pos[1])
            return True
        time.sleep(0.5)
    return False


# 仮想カメラが今動いているか（取得失敗時は False 扱い）
def _is_vcam_active(client):
    try:
        return bool(client.get_virtual_cam_status().output_active)
    except Exception:
        return False


# OBSへ接続するクライアントを作る（接続時に .env から読む）
def _obs_client():
    port = int(os.environ.get("OBS_WS_PORT", "4455"))
    password = os.environ.get("OBS_WS_PASSWORD", "")
    try:
        return obs.ReqClient(host=OBS_HOST, port=port, password=password, timeout=5)
    except Exception as e:
        raise RuntimeError(f"OBSに接続できません（OBS起動とWebSocket設定を確認）: {e}")


def start_live():
    """配信開始：ゲーム起動 → OBS仮想カメラ → DiscordカメラON"""
    if _state["live"]:
        raise RuntimeError("すでに配信中です。先に /live_stop してください。")

    # 1. ゲームを起動（開いたまま保持。cwdをProjectにしないとResourcesを読めない）
    game = subprocess.Popen([str(test_run.GAME_EXE)], cwd=str(test_run.PROJECT_DIR))
    _state["game"] = game
    time.sleep(WARMUP_SEC)  # 描画とOBSの捕捉が安定するまで待つ

    # 2. OBSの仮想カメラを開始（既に起動済みなら何もしない。500が出ても実際に点いていればOK扱い）
    client = _obs_client()
    if not _is_vcam_active(client):
        try:
            client.start_virtual_cam()
        except Exception:
            time.sleep(1)
            if not _is_vcam_active(client):  # 500後に再確認して、点いていなければ本当の失敗
                raise RuntimeError("OBS仮想カメラを開始できませんでした")
    time.sleep(1)

    # 3. J.A.R.V.I.S.のVCに参加（入り忘れ防止。既に入っていても無害）
    _send_hotkey(VC_HOTKEY)
    time.sleep(1)

    # 4. カメラ確認モーダルを確実にクリックするため、Discordを一時的にプライマリモニターへ移動
    #    （サブモニターは拡大率違いでクリック座標がズレるため。押したら元へ戻す）
    dw = _get_discord_window()
    orig_pos = None
    if dw:
        try:
            orig_pos = (dw.left, dw.top)
            if dw.isMaximized:
                dw.restore()
            dw.moveTo(80, 80)  # プライマリ左上付近へ
            dw.activate()
        except Exception:
            orig_pos = None
    time.sleep(0.8)

    # 5. カメラ呼び出し（F10）→ モーダルがプライマリ上に出る
    _send_hotkey(CAM_HOTKEY)
    time.sleep(1.0)

    # 6. 「カメラをオン」ボタンを画像認識でクリック（プライマリ上なので座標が合う）
    clicked = _click_camera_on()

    # 7. Discordを元の位置（サブモニター）へ戻す
    if dw and orig_pos:
        try:
            dw.moveTo(orig_pos[0], orig_pos[1])
        except Exception:
            pass

    if not clicked:
        raise RuntimeError(
            "「カメラをオン」ボタンが見つかりませんでした"
            "（cam_on_button.png をプライマリ上で撮り直し・opencv導入 を確認）"
        )

    _state["live"] = True
    return {"ok": True}


def stop_live():
    """配信停止：DiscordカメラOFF → OBS仮想カメラ停止 → ゲーム終了"""
    if not _state["live"]:
        raise RuntimeError("配信していません。")

    # 1. DiscordのカメラをOFF（同じホットキーで切替）
    _send_hotkey(CAM_HOTKEY)
    time.sleep(0.5)

    # 2. VCから切断（Ctrl+Shift+F12）
    _send_hotkey(DISCONNECT_HOTKEY)
    time.sleep(0.3)

    # 3. OBSの仮想カメラを停止（動いている時だけ。失敗しても先へ進む）
    try:
        client = _obs_client()
        if _is_vcam_active(client):
            client.stop_virtual_cam()
    except Exception:
        pass

    # 4. ゲームを終了
    game = _state.get("game")
    if game:
        game.terminate()

    _state["game"] = None
    _state["live"] = False
    return {"ok": True}


# 単体テスト用：開始 → 10秒 → 停止
if __name__ == "__main__":
    from dotenv import load_dotenv
    from pathlib import Path
    load_dotenv(Path(__file__).resolve().parents[3] / ".env")
    print("start:", start_live())
    time.sleep(10)
    print("stop:", stop_live())
