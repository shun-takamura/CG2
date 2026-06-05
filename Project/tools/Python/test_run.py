import subprocess
import time
from pathlib import Path

import pyautogui
import pydirectinput
import pygetwindow as gw

# このスクリプト自身の場所から、必要なフォルダを割り出す
#   tools/Python/test_run.py を基準に
#   .parent(=Python) .parent(=tools) .parent(=Project)
PROJECT_DIR = Path(__file__).resolve().parent.parent.parent  # Project フォルダ
REPO_DIR = PROJECT_DIR.parent                                 # リポジトリ直下(CG2)
OUT_DIR = Path(__file__).resolve().parent                     # 出力先(このフォルダ)

# ビルド済みゲーム本体（Debug版）
GAME_EXE = REPO_DIR / "Generated" / "Output" / "Debug" / "CG2_0_1.exe"

# ゲームのウィンドウタイトル（Framework.cpp で設定）
WINDOW_TITLE = "QR"

# 起動から撮影までの待機秒数（描画が安定するまで待つ）
WAIT_SEC = 5

# Discord添付対策：横幅がこれを超えたら縮小する（縦横比は維持）
MAX_WIDTH = 1280

# 撮影前に送るキー（順番に押す） Q=強攻撃 E=弱攻撃 space=回避
KEYS = ["q", "e", "space"]


def capture(win, path):
    """ウィンドウの範囲だけ撮影し、大きければ縮小して保存する"""
    region = (win.left, win.top, win.width, win.height)
    image = pyautogui.screenshot(region=region)
    if image.width > MAX_WIDTH:
        ratio = MAX_WIDTH / image.width
        image = image.resize((MAX_WIDTH, int(image.height * ratio)))
    image.save(path)
    print(f"スクショ保存: {path}（{image.width}x{image.height}）")


# ゲームを起動する（cwd を Project にしないと Resources を読めずに落ちる）
game = subprocess.Popen([str(GAME_EXE)], cwd=str(PROJECT_DIR))

# 描画が安定するまで待つ
time.sleep(WAIT_SEC)

# タイトルでゲームウィンドウを探す
windows = gw.getWindowsWithTitle(WINDOW_TITLE)
if not windows:
    print(f"ウィンドウ '{WINDOW_TITLE}' が見つかりませんでした")
    game.terminate()
    raise SystemExit(1)
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
capture(win, OUT_DIR / "before.png")

# キーを順番に押す（pydirectinput がスキャンコードを送る）
for key in KEYS:
    pydirectinput.press(key)
    time.sleep(0.3)

# 入力した後の状態を撮影
capture(win, OUT_DIR / "after.png")

# ゲームを閉じる
game.terminate()
print("ゲーム終了")
