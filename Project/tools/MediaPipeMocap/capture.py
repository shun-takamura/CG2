"""MediaPipe Pose (Tasks API) によるキャプチャ。

- run_webcam(): デバイスカメラからライブキャプチャ。stop_event で停止。
- run_video():  動画ファイルを最初から最後まで処理。

どちらも frames = [(time_sec, [(x, y, z, visibility) x 33]), ...] を返す。
座標は pose_world_landmarks（メートル、腰中心）そのまま。
プレビューは OpenCV ウィンドウにランドマークをオーバーレイ表示する。

モデル (pose_landmarker_full.task) は初回実行時に models/ へ自動ダウンロードする。
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

# exe化(PyInstaller --onedir)時は実行ファイルの隣、ソース実行時はこのファイルの隣に
# models/ を置く（sys._MEIPASSは一時展開先なので永続保存には使えない）
if getattr(sys, "frozen", False):
    _APP_DIR = Path(sys.executable).parent
else:
    _APP_DIR = Path(__file__).parent
MODEL_PATH = _APP_DIR / "models" / "pose_landmarker_full.task"

MODEL_DOWNLOAD_RETRIES = 3
MODEL_DOWNLOAD_RETRY_WAIT_SEC = 2.0

PREVIEW_WINDOW = "MediaPipeMocap Preview  (Q: stop)"

# プレビュー描画用の骨格接続（MediaPipe POSE_CONNECTIONS 相当の主要部分）
_CONNECTIONS = [
    (11, 12), (11, 13), (13, 15), (12, 14), (14, 16),       # 肩・腕
    (11, 23), (12, 24), (23, 24),                            # 胴体
    (23, 25), (25, 27), (27, 31), (24, 26), (26, 28), (28, 32),  # 脚
]


def _ensure_model(status_cb=None):
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


def _create_landmarker(status_cb=None):
    _ensure_model(status_cb)
    options = vision.PoseLandmarkerOptions(
        base_options=mp_python.BaseOptions(model_asset_path=str(MODEL_PATH)),
        running_mode=vision.RunningMode.VIDEO,
        num_poses=1,
    )
    return vision.PoseLandmarker.create_from_options(options)


def _setup_preview(rect):
    """プレビューウィンドウをリサイズ可能で作り、保存済み位置・サイズを復元する。"""
    cv2.namedWindow(PREVIEW_WINDOW, cv2.WINDOW_NORMAL)
    if rect and len(rect) == 4:
        x, y, w, h = rect
        if w > 0 and h > 0:
            cv2.resizeWindow(PREVIEW_WINDOW, int(w), int(h))
            cv2.moveWindow(PREVIEW_WINDOW, int(x), int(y))


def _report_preview_rect(cb):
    """現在のプレビューウィンドウの [x, y, w, h] をコールバックへ渡す（破棄前に呼ぶ）。"""
    if cb is None:
        return
    try:
        x, y, w, h = cv2.getWindowImageRect(PREVIEW_WINDOW)
    except cv2.error:
        return
    if w > 0 and h > 0:
        cb([int(x), int(y), int(w), int(h)])


def _draw_pose(frame_bgr, norm_landmarks):
    """正規化画像座標のランドマークをプレビューに描画"""
    h, w = frame_bgr.shape[:2]
    pts = [(int(lm.x * w), int(lm.y * h)) for lm in norm_landmarks]
    for a, b in _CONNECTIONS:
        cv2.line(frame_bgr, pts[a], pts[b], (0, 255, 0), 2)
    for p in pts:
        cv2.circle(frame_bgr, p, 3, (0, 128, 255), -1)


def _detect(landmarker, frame_bgr, timestamp_ms):
    """1 フレーム推定。world ランドマーク list（未検出なら None）を返しプレビューに描画"""
    rgb = cv2.cvtColor(frame_bgr, cv2.COLOR_BGR2RGB)
    mp_image = mp.Image(image_format=mp.ImageFormat.SRGB, data=rgb)
    result = landmarker.detect_for_video(mp_image, timestamp_ms)

    if not result.pose_world_landmarks:
        return None
    if result.pose_landmarks:
        _draw_pose(frame_bgr, result.pose_landmarks[0])
    world = result.pose_world_landmarks[0]
    return [(lm.x, lm.y, lm.z,
             lm.visibility if lm.visibility is not None else 1.0)
            for lm in world]


def run_webcam(stop_event, camera_index=0, status_cb=None,
               preview_rect=None, preview_rect_cb=None):
    """Web カメラからライブキャプチャする。stop_event.set() か Q キーで停止。

    preview_rect:    復元するプレビューウィンドウの [x, y, w, h]（None=OS任せ）
    preview_rect_cb: 終了時に現在の [x, y, w, h] を渡すコールバック
    """
    landmarker = _create_landmarker(status_cb)
    cap = cv2.VideoCapture(camera_index, cv2.CAP_DSHOW)
    if not cap.isOpened():
        landmarker.close()
        raise RuntimeError(f"カメラ {camera_index} を開けませんでした")

    _setup_preview(preview_rect)
    frames = []
    t0 = time.perf_counter()
    last_ts = -1
    try:
        while not stop_event.is_set():
            ok, frame = cap.read()
            if not ok:
                break
            t = time.perf_counter() - t0
            ts_ms = max(int(t * 1000), last_ts + 1)  # 単調増加を保証
            last_ts = ts_ms
            lm = _detect(landmarker, frame, ts_ms)
            if lm is not None:
                frames.append((t, lm))

            cv2.putText(frame, f"REC {t:6.2f}s  frames:{len(frames)}",
                        (10, 30), cv2.FONT_HERSHEY_SIMPLEX, 0.8, (0, 0, 255), 2)
            cv2.imshow(PREVIEW_WINDOW, frame)
            if cv2.waitKey(1) & 0xFF in (ord("q"), ord("Q")):
                break
            if status_cb:
                status_cb(f"録画中… {t:.1f}s / {len(frames)} フレーム")
    finally:
        _report_preview_rect(preview_rect_cb)
        cap.release()
        cv2.destroyAllWindows()
        landmarker.close()
    return frames


def run_video(video_path, stop_event=None, status_cb=None, show_preview=True,
              preview_rect=None, preview_rect_cb=None):
    """動画ファイルを全フレーム処理する。時刻はフレーム index / fps。

    stop_event.set() か Q キーで途中キャンセルできる（それまでのフレームを返す）。
    preview_rect / preview_rect_cb はプレビューウィンドウの位置・サイズ復元/保存用。
    """
    landmarker = _create_landmarker(status_cb)
    cap = cv2.VideoCapture(str(video_path))
    if not cap.isOpened():
        landmarker.close()
        raise RuntimeError(f"動画を開けませんでした: {video_path}")

    fps = cap.get(cv2.CAP_PROP_FPS) or 30.0
    if fps <= 0:
        fps = 30.0
    total = int(cap.get(cv2.CAP_PROP_FRAME_COUNT))

    if show_preview:
        _setup_preview(preview_rect)
    frames = []
    idx = 0
    last_ts = -1
    t_start = time.perf_counter()
    try:
        while True:
            if stop_event is not None and stop_event.is_set():
                break
            ok, frame = cap.read()
            if not ok:
                break
            t = idx / fps
            ts_ms = max(int(t * 1000), last_ts + 1)  # 単調増加を保証
            last_ts = ts_ms
            lm = _detect(landmarker, frame, ts_ms)
            if lm is not None:
                frames.append((t, lm))

            if show_preview:
                cv2.putText(frame, f"{idx}/{total}", (10, 30),
                            cv2.FONT_HERSHEY_SIMPLEX, 0.8, (0, 255, 0), 2)
                cv2.imshow(PREVIEW_WINDOW, frame)
                if cv2.waitKey(1) & 0xFF in (ord("q"), ord("Q")):
                    break
            if status_cb and idx % 10 == 0 and total > 0:
                elapsed = time.perf_counter() - t_start
                progress = idx / total
                eta = elapsed / progress - elapsed if progress > 0 else 0.0
                status_cb(f"処理中… {idx}/{total} ({progress * 100:.0f}%) "
                         f"残り約{eta:.0f}秒")
            idx += 1
    finally:
        if show_preview:
            _report_preview_rect(preview_rect_cb)
        cap.release()
        cv2.destroyAllWindows()
        landmarker.close()
    return frames


def list_cameras(max_index=8):
    """開けるカメラのインデックス一覧を返す（各インデックスを実際に開いて1フレーム読めるか確認）。

    プローブに時間がかかる(各インデックスで数百ms)ため、頻繁には呼ばない前提。
    """
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
