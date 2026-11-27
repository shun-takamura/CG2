"""自機（PlayerModel）の体のクリップの表。まとまり（グループ）ごとに 1 ファイル（25_PlayerAnimClips.md）。

このフォルダの <まとまり>.py が 1 つのまとまり。中身は 2 つの表だけ：
    POSES = {姿勢の名前: 姿勢}       … 書き方は rig_player_mech.py の pose_body（25 番 §3）
    CLIPS = {クリップの名前: クリップ} … エンジンでは PlayerModel_<クリップの名前>.anim

    rig_player_mech.py --body-clips ... --group <まとまり> --export ...  で、そのまとまりのクリップだけを
    PlayerModel@<まとまり>.gltf（アーマチュアだけ）に書き出す。ほかのまとまりのファイルは読むが、書き出さない。

決まり：
    ・姿勢・クリップの名前はすべてのまとまりで重ならないこと（重なると rig_player_mech.py が止まる）
    ・ほかのまとまりの姿勢は "base" やクリップの keys で使ってよいが、ほかのまとまりのファイルは直さない
"""

import importlib
from pathlib import Path

# 書き出しの順（ここに無いファイルは後ろに名前順で足す）
GROUP_ORDER = ("start", "melee", "move", "shoot", "defense", "special")


def group_names():
    """このフォルダにあるまとまりの名前（GROUP_ORDER の順）"""
    found = sorted(p.stem for p in Path(__file__).parent.glob("*.py") if not p.stem.startswith("_"))
    return [g for g in GROUP_ORDER if g in found] + [g for g in found if g not in GROUP_ORDER]


def load_groups():
    """{まとまりの名前: モジュール}。モジュールは POSES と CLIPS を持つ"""
    groups = {}
    for name in group_names():
        module = importlib.import_module(f"{__name__}.{name}")
        for table in ("POSES", "CLIPS"):
            if not isinstance(getattr(module, table, None), dict):
                raise SystemExit(f"[player_clips] {name}.py に {table} = {{}} が無い")
        groups[name] = module
    return groups
