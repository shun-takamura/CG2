"""ユーザー設定の永続化。カメラ選択とスムージングON/OFFのみを対象にした最小限の仕組み。

exe化時は実行ファイルの隣、ソース実行時はこのファイルの隣に settings.json を置く。
"""

from __future__ import annotations

import json
import sys
from pathlib import Path

if getattr(sys, "frozen", False):
    _APP_DIR = Path(sys.executable).parent
else:
    _APP_DIR = Path(__file__).parent

SETTINGS_PATH = _APP_DIR / "settings.json"

_DEFAULTS = {
    "camera_index": 0,
    "smoothing": True,
    "preview_rect": None,   # プレビューウィンドウの [x, y, w, h]（None=OS任せ）
    "blender_path": None,   # Blender 自動起動用の明示パス（None=自動検出）
    "reduce_keys": False,   # 出力時にキー削減をかけるか
    "tolerance_deg": 0.5,   # キー削減の許容角度誤差
}


def load() -> dict:
    if not SETTINGS_PATH.exists():
        return dict(_DEFAULTS)
    try:
        data = json.loads(SETTINGS_PATH.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return dict(_DEFAULTS)
    merged = dict(_DEFAULTS)
    merged.update({k: v for k, v in data.items() if k in _DEFAULTS})
    return merged


def save(settings: dict) -> None:
    try:
        SETTINGS_PATH.write_text(
            json.dumps({k: settings[k] for k in _DEFAULTS if k in settings},
                      indent=2, ensure_ascii=False),
            encoding="utf-8")
    except OSError:
        pass  # 設定保存の失敗はアプリの動作に影響させない
