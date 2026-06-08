"""J.A.R.V.I.S. Step1/4: ゲーム自動起動＆入力テスト＆スクショ

ゲームを起動 → 待機 → 起動直後を撮影 → キー入力 → 入力後を撮影 → 終了。
Step4ではこの run_test() を Discord Bot から呼び出して使う。
単体でも動く（末尾の __main__）。
"""

import subprocess
import time
from pathlib import Path

import pyautogui
import pydirectinput
import pygetwindow as gw

# このスクリプト自身の場所から、必要なフォルダを割り出す
#   tools/Python/test_run.py → .parent(Python) .parent(tools) .parent(Project)
PROJECT_DIR = Path(__file__).resolve().parent.parent.parent  # Project フォルダ
REPO_DIR = PROJECT_DIR.parent                                 # リポジトリ直下(CG2)
OUT_DIR = Path(__file__).resolve().parent                     # 出力先(このフォルダ)

# ビルド済みゲーム本体（Debug版）
GAME_EXE = REPO_DIR / "Generated" / "Output" / "Debug" / "CG2_0_1.exe"

# ゲームのウィンドウタイトル（Framework.cpp で設定）
WINDOW_TITLE = "QR"

# 既定値（引数省略時に使う）
DEFAULT_WAIT_SEC = 5            # 起動から撮影までの待機秒数（描画が安定するまで）
DEFAULT_KEYS = ["q", "e", "space"]  # Q=強攻撃 E=弱攻撃 space=回避

# Discord添付対策：横幅がこれを超えたら縮小する（縦横比は維持）
MAX_WIDTH = 1280


def capture(win, path):
    """ウィンドウの範囲だけ撮影し、大きければ縮小して保存する"""
    region = (win.left, win.top, win.width, win.height)
    image = pyautogui.screenshot(region=region)
    if image.width > MAX_WIDTH:
        ratio = MAX_WIDTH / image.width
        image = image.resize((MAX_WIDTH, int(image.height * ratio)))
    image.save(path)
    print(f"スクショ保存: {path}（{image.width}x{image.height}）")


def run_test(keys=None, wait_sec=DEFAULT_WAIT_SEC):
    """ゲームを起動してキー入力し、前後のスクショを撮る。
    成功時は撮った画像のパスと押したキーを辞書で返す。
    ウィンドウが見つからない等の失敗時は RuntimeError を投げる（Bot側で捕まえて通知する）。
    """
    keys = keys or DEFAULT_KEYS
    before_path = OUT_DIR / "before.png"
    after_path = OUT_DIR / "after.png"

    # ゲームを起動（cwd を Project にしないと Resources を読めずに落ちる）
    game = subprocess.Popen([str(GAME_EXE)], cwd=str(PROJECT_DIR))
    try:
        # 描画が安定するまで待つ
        time.sleep(wait_sec)

        # タイトルでゲームウィンドウを探す
        windows = gw.getWindowsWithTitle(WINDOW_TITLE)
        if not windows:
            raise RuntimeError(f"ウィンドウ '{WINDOW_TITLE}' が見つかりませんでした")
        win = windows[0]

        # 最小化されていると座標が取れないので、戻して前面に出す
        if win.isMinimized:
            win.restore()
        try:
            win.activate()  # 前面に出す（OSに拒否されても続行）
        except Exception:
            pass
        time.sleep(0.5)

        # 入力する前の状態を撮影
        capture(win, before_path)

        # キーを順番に押す（pydirectinput がスキャンコードを送る）
        for key in keys:
            pydirectinput.press(key)
            time.sleep(0.3)

        # 入力した後の状態を撮影
        capture(win, after_path)
    finally:
        # 成否に関わらずゲームを必ず閉じる（プロセスの取り残しを防ぐ）
        game.terminate()
        print("ゲーム終了")

    return {"before": before_path, "after": after_path, "keys": keys}


# 直接実行時のみ既定値で1回走らせる
if __name__ == "__main__":
    run_test()
