"""キャプチャ結果（rig.solve() の戻り値）を .mocapdata.json として保存/読込する。

セッションをまたいでリターゲットできるようにするための中間フォーマット。
JSONなのでツール外からも中身を確認・加工できる。
"""

from __future__ import annotations

import json
from pathlib import Path

FORMAT_VERSION = 1
SUFFIX = ".mocapdata.json"


def save(result: dict, path) -> Path:
    """rig.solve() の結果を保存する。拡張子は .mocapdata.json に揃える。"""
    p = Path(path)
    if not p.name.endswith(SUFFIX):
        p = p.with_name(p.stem + SUFFIX)
    data = {
        "format_version": FORMAT_VERSION,
        "joints": result["joints"],
        "times": result["times"],
        "rot_keys": result["rot_keys"],
        "q_global_keys": result.get("q_global_keys", {}),
        "root_trans_keys": result["root_trans_keys"],
        "duration": result["duration"],
    }
    p.write_text(json.dumps(data), encoding="utf-8")
    return p


def load(path) -> dict:
    """保存済み .mocapdata.json を rig.solve() 互換の dict として読み込む。"""
    data = json.loads(Path(path).read_text(encoding="utf-8"))
    version = data.get("format_version")
    if version != FORMAT_VERSION:
        raise ValueError(f"未対応の mocapdata バージョンです: {version}")
    if not data.get("q_global_keys"):
        raise ValueError("このファイルにはグローバル回転が含まれていません。"
                         "新しいバージョンのアプリで再キャプチャしてください。")

    # JSON はタプルをリストに落とすが、rig/retarget のベクトル・クォータニオン演算は
    # インデックスアクセスのみなのでリストのままで問題ない
    return {
        "joints": data["joints"],
        "times": data["times"],
        "rot_keys": data["rot_keys"],
        "q_global_keys": data["q_global_keys"],
        "root_trans_keys": data["root_trans_keys"],
        "duration": data["duration"],
    }
