"""MediaPipe Pose 推定のコア部分（プレビュー表示方式に依存しない共有ロジック）。

`capture.py`（スタンドアロンアプリ、cv2.imshowでプレビュー）と
Blenderアドオンの capture_operator.py（ビューポートへGPU描画でプレビュー）の
両方から使われる。モデルのダウンロード・ランドマーカー生成・1フレーム推定
・骨格オーバーレイ描画・カメラ列挙だけを担当し、ウィンドウ表示やキャプチャ
ループ自体はそれぞれの呼び出し側が持つ。
"""

from __future__ import annotations

import sys
import time
import urllib.error
import urllib.request
from pathlib import Path

import cv2
import mediapipe as mp
from mediapipe.tasks import python as mp_python
from mediapipe.tasks.python import vision

MODEL_URL = ("https://storage.googleapis.com/mediapipe-models/pose_landmarker/"
             "pose_landmarker_full/float16/latest/pose_landmarker_full.task")

if getattr(sys, "frozen", False):
    _APP_DIR = Path(sys.executable).parent
else:
    _APP_DIR = Path(__file__).parent
MODEL_PATH = _APP_DIR / "models" / "pose_landmarker_full.task"

MODEL_DOWNLOAD_RETRIES = 3
MODEL_DOWNLOAD_RETRY_WAIT_SEC = 2.0

# プレビュー描画用の骨格接続（MediaPipe POSE_CONNECTIONS 相当の主要部分）
CONNECTIONS = [
    (11, 12), (11, 13), (13, 15), (12, 14), (14, 16),       # 肩・腕
    (11, 23), (12, 24), (23, 24),                            # 胴体
    (23, 25), (25, 27), (27, 31), (24, 26), (26, 28), (28, 32),  # 脚
]


def ensure_model(status_cb=None):
    if MODEL_PATH.exists():
        return
    MODEL_PATH.parent.mkdir(parents=True, exist_ok=True)
    tmp = MODEL_PATH.with_suffix(".tmp")

    last_error = None
    for attempt in range(1, MODEL_DOWNLOAD_RETRIES + 1):
        if status_cb:
            suffix = "" if attempt == 1 else f"（{attempt}/{MODEL_DOWNLOAD_RETRIES}回目）"
            status_cb(f"姿勢推定モデルをダウンロード中…{suffix}")
        try:
            urllib.request.urlretrieve(MODEL_URL, tmp)
            tmp.rename(MODEL_PATH)
            return
        except (urllib.error.URLError, OSError, TimeoutError) as e:
            last_error = e
            tmp.unlink(missing_ok=True)
            if attempt < MODEL_DOWNLOAD_RETRIES:
                time.sleep(MODEL_DOWNLOAD_RETRY_WAIT_SEC)

    raise RuntimeError(
        f"姿勢推定モデルのダウンロードに失敗しました（{MODEL_DOWNLOAD_RETRIES}回試行）: {last_error}\n"
        f"手動で下記URLからダウンロードし、\n{MODEL_URL}\n"
        f"次の場所に置いてください:\n{MODEL_PATH}")


def create_landmarker(status_cb=None):
    ensure_model(status_cb)
    options = vision.PoseLandmarkerOptions(
        base_options=mp_python.BaseOptions(model_asset_path=str(MODEL_PATH)),
        running_mode=vision.RunningMode.VIDEO,
        num_poses=1,
    )
    return vision.PoseLandmarker.create_from_options(options)


def draw_pose(frame_bgr, norm_landmarks):
    """正規化画像座標のランドマークをフレームに描画する"""
    h, w = frame_bgr.shape[:2]
    pts = [(int(lm.x * w), int(lm.y * h)) for lm in norm_landmarks]
    for a, b in CONNECTIONS:
        cv2.line(frame_bgr, pts[a], pts[b], (0, 255, 0), 2)
    for p in pts:
        cv2.circle(frame_bgr, p, 3, (0, 128, 255), -1)


def detect(landmarker, frame_bgr, timestamp_ms):
    """1 フレーム推定。world ランドマーク list（未検出なら None）を返し、
    frame_bgr に骨格オーバーレイを描画する（呼び出し側の配列を直接書き換える）。
    """
    rgb = cv2.cvtColor(frame_bgr, cv2.COLOR_BGR2RGB)
    mp_image = mp.Image(image_format=mp.ImageFormat.SRGB, data=rgb)
    result = landmarker.detect_for_video(mp_image, timestamp_ms)

    if not result.pose_world_landmarks:
        return None
    if result.pose_landmarks:
        draw_pose(frame_bgr, result.pose_landmarks[0])
    world = result.pose_world_landmarks[0]
    return [(lm.x, lm.y, lm.z,
             lm.visibility if lm.visibility is not None else 1.0)
            for lm in world]


def list_cameras(max_index=8):
    """開けるカメラのインデックス一覧を返す（各インデックスを実際に開いて1フレーム読めるか確認）。"""
    found = []
    for i in range(max_index):
        cap = cv2.VideoCapture(i, cv2.CAP_DSHOW)
        try:
            if cap.isOpened():
                ok, _ = cap.read()
                if ok:
                    found.append(i)
        finally:
            cap.release()
    return found
