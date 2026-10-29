#!/usr/bin/env python3
"""谷（120〜180 秒）の障害物・敵・飾りの岩を、秒ごとの表から置く（11_LowAltitudeFlight.md の谷 / 4_StageGimmick.md）。

レールはエンジンと同じ Catmull-Rom を制御点の番号で等分してたどる（SplineCurveActor::Sample：
s = 秒 / 180 × (点の数 − 1)）。表の「左右・上下」はその秒のカメラ（レールの上）から見た右・上 [m]。
自機はカメラの 12m 前で、動けるのはレールのまわり 左右 ±5.4m・上下 −3.8〜+2.2m（11 番 §5.1）。

書き込む所（キー単位の所有。11 番 §3.1 の 4.）:
    Resources/Json/Waves/stage1.json   … trigger_sec >= OWN_FROM_SEC のエントリだけを作り直す（それより前は触らない）
    Resources/Json/Scenes/StagePlay.json … 名前が CanyonDecor_ で始まるエントリだけを作り直す（--scene のとき）

使い方（Project/ で実行）:
    python tools/Python/gen_canyon_wave.py            # 書き込み
    python tools/Python/gen_canyon_wave.py --scene    # 飾りの岩もシーン JSON に書く
    python tools/Python/gen_canyon_wave.py --dry-run  # 置く物の一覧だけ

障害物（enemy_type "Obstacle"）の向き:
    梁は rotate_deg = (0, 0, 90 + 傾き)。Z 軸だけで倒す（描画は Rx·Ry·Rz、当たり判定は Rz·Ry·Rx で、
    2 軸以上を回すと食い違うため）。梁はワールドの X 方向に横たわり、傾き > 0 で右（+X）が上がる。
"""

import argparse
import json
import math
from pathlib import Path

import numpy as np

SCENE_PATH = Path("Resources/Json/Scenes/StagePlay.json")
WAVE_PATH = Path("Resources/Json/Waves/stage1.json")
HEIGHT_NPY = Path("Generated/Terrain/RiverTerrain/RiverTerrain_Height.npy")
HEIGHT_JSON = Path("Generated/Terrain/RiverTerrain/RiverTerrain_Height.json")

STAGE_SEC = 180.0
OWN_FROM_SEC = 118.0        # これ以降の Wave エントリはこのスクリプトが持つ
LEAD_SEC = 8.0              # 障害物・砲台はカメラが通る何秒前に出すか（谷は 18m/s ≈ 145m 手前）
PASS_SEC = 3.0              # カメラが通ってから何秒で消すか（retreat_sec。後ろに残った物を描かない。14 番 §4.6）
WALL_GAP = 1.5              # 砲台を壁の面からどれだけ離すか [m]
WALL_FALLBACK = 26.0        # 高さマップが無い時の壁までの距離 [m]（11 番 §3.0 の峡谷の形）
TURRET_SHOOT_SEC = 2.5
HOVER_SHOOT_SEC = 3.0

# ---------------------------------------------------------------- 配置の表（秒 = カメラがそこを通る時刻）
# 導入（125〜138）→ 梁（138〜152）→ 山場（152〜168）→ 出口（168〜180 は何も置かない）
SPIRES = [  # (秒, プレハブ, 右 [m])
    (127.0, "canyon_spire_s", 6.0),
    (130.0, "canyon_spire_s", -6.0),
    (133.0, "canyon_spire_l", 8.0),
    (136.0, "canyon_spire_l", -8.0),
    (147.0, "canyon_spire_s", 6.0),
    (154.0, "canyon_spire_l", -8.0),
    (154.5, "canyon_spire_s", 7.0),
    (160.0, "canyon_spire_l", 8.0),
    (166.0, "canyon_spire_s", -6.0),
    (166.5, "canyon_spire_s", 6.5),
]
BEAMS = [  # (秒, 上 [m]（レールと交わる所の梁の中心）, 傾き [度])
    (140.0, -5.0, 18.0),
    (144.0, -5.5, 16.0), (144.0, -5.5, -16.0),   # V（上を抜ける）
    (150.0, 5.5, 16.0), (150.0, 5.5, -16.0),     # Λ（下を抜ける）
    (157.0, -5.0, -18.0),
    (163.0, -5.5, 16.0), (163.0, -5.5, -16.0),
]
TURRETS = [  # (秒, 壁の側 +1=右 / -1=左, 上 [m])
    (131.0, 1.0, 2.0),
    (135.0, -1.0, 1.0),
    (153.0, -1.0, 2.0),
    (158.0, 1.0, 0.0),
    (163.0, -1.0, 1.0),
]
HOVERS = [  # (出る秒, カメラから見た位置 (右, 上, 前))
    (141.0, (-6.0, 2.0, 30.0)),
    (146.0, (6.0, 1.0, 30.0)),
    (152.0, (0.0, 3.0, 32.0)),
    (158.0, (-6.0, 1.0, 30.0)),
    (164.0, (6.0, 2.0, 30.0)),
]
# 飾りの低い岩（当たり判定なし。先は飛行高度の 15m 下）。谷底の通り道の外に左右交互に置く
DECOR_FROM, DECOR_TO, DECOR_STEP = 124.0, 177.0, 3.5
DECOR_SIDE_RANGE = (12.0, 19.0)
DECOR_SEED = 29


# ---------------------------------------------------------------- レール
def load_rail(doc):
    for o in doc.get("objects", []):
        if o.get("name") == "CameraPath":
            return [np.array(p, dtype=np.float64) for p in o["points"]]
    raise SystemExit("CameraPath が見つからない")


def rail_at(rail, sec):
    """秒 → (位置, 前, 右, 上)。SplineCurveActor::Sample と同じ割り方"""
    n = len(rail)
    s = min(max(sec / STAGE_SEC, 0.0), 1.0) * (n - 1)

    def sample(s):
        seg = min(int(math.floor(s)), n - 2)
        t = s - seg
        p0, p1 = rail[max(seg - 1, 0)], rail[seg]
        p2, p3 = rail[seg + 1], rail[min(seg + 2, n - 1)]
        return 0.5 * (2 * p1 + (-p0 + p2) * t + (2 * p0 - 5 * p1 + 4 * p2 - p3) * t * t
                      + (-p0 + 3 * p1 - 3 * p2 + p3) * t ** 3)

    p = sample(s)
    f = sample(min(s + 0.02, n - 1)) - sample(max(s - 0.02, 0.0))
    f /= np.linalg.norm(f)
    right = np.array([f[2], 0.0, -f[0]])
    right /= np.linalg.norm(right)
    up = np.cross(f, right)  # 左手系（+Z 前・+Y 上・+X 右）
    return p, f, right, up


# ---------------------------------------------------------------- 壁
class Walls:
    def __init__(self, root: Path):
        self.h = None
        if (root / HEIGHT_NPY).exists() and (root / HEIGHT_JSON).exists():
            self.h = np.load(root / HEIGHT_NPY)
            self.info = json.loads((root / HEIGHT_JSON).read_text(encoding="utf-8"))
        else:
            print(f"注意: {HEIGHT_NPY} が無いので壁までを {WALL_FALLBACK}m とみなす（gen_terrain.py を一度流すとできる）")

    def height(self, x, z):
        i = self.info
        col = (x - i["x_min"]) / i["cell_m"]
        row = (i["z_max"] - z) / i["cell_m"]
        r, c = int(round(row)), int(round(col))
        if not (0 <= r < self.h.shape[0] and 0 <= c < self.h.shape[1]):
            return float("nan")
        return float(self.h[r, c])

    def distance(self, p, direction, y):
        """p から水平の direction へ、高さ y で地面に当たるまでの距離"""
        if self.h is None:
            return WALL_FALLBACK
        d = 0.0
        while d < 200.0:
            g = self.height(p[0] + direction[0] * d, p[2] + direction[2] * d)
            if math.isnan(g) or g > y:
                return d
            d += 0.5
        return d


# ---------------------------------------------------------------- エントリ
def entry(enemy_type, prefab, trigger, **extra):
    e = {
        "enemy_type": enemy_type, "prefab": prefab, "trigger_sec": round(trigger, 2), "retreat_sec": -1.0,
        "traverse_sec": 8.0, "spline_id": "", "count": 1, "shoot_interval_sec": 0.0,
        "spawn_interval_sec": 5.0, "spawn_limit": 4, "child_prefab": "", "child_spline_id": "",
    }
    e.update(extra)
    return e


def r3(v):
    return [round(float(c), 3) for c in v]


def build_entries(rail, walls):
    out = []
    for sec, prefab, side in SPIRES:
        p, f, right, up = rail_at(rail, sec)
        pos = p + right * side
        out.append(entry("Obstacle", prefab, sec - LEAD_SEC, positions=[r3(pos)], retreat_sec=sec + PASS_SEC))
    for sec, dy, tilt in BEAMS:
        p, f, right, up = rail_at(rail, sec)
        pos = p + np.array([0.0, dy, 0.0])
        out.append(entry("Obstacle", "canyon_beam", sec - LEAD_SEC, positions=[r3(pos)], retreat_sec=sec + PASS_SEC,
                         rotate_deg=[0.0, 0.0, round(90.0 + tilt, 2)]))
    for sec, side, dy in TURRETS:
        p, f, right, up = rail_at(rail, sec)
        y = p[1] + dy
        d = walls.distance(p, right * side, y)
        pos = p + right * side * max(d - WALL_GAP, 6.0)
        pos[1] = y
        out.append(entry("Drone", "turret", sec - LEAD_SEC, positions=[r3(pos)], shoot_interval_sec=TURRET_SHOOT_SEC,
                         retreat_sec=sec + PASS_SEC))
    for sec, off in HOVERS:
        out.append(entry("Drone", "HoverEnemy", sec, shoot_interval_sec=HOVER_SHOOT_SEC, camera_offset=list(off)))
    out.sort(key=lambda e: e["trigger_sec"])
    return out


def build_decor(rail):
    rng = np.random.default_rng(DECOR_SEED)
    objs = []
    sec, side, i = DECOR_FROM, 1.0, 0
    while sec <= DECOR_TO:
        p, f, right, up = rail_at(rail, sec + rng.uniform(-0.8, 0.8))
        pos = p + right * side * rng.uniform(*DECOR_SIDE_RANGE)
        objs.append({
            "type": "Object3D",
            "name": f"CanyonDecor_{i:02d}",
            "tag": "None",
            "dir": "Resources/Models/CanyonRocks",
            "file": "CanyonSpireLow.mesh",
            "streamGroup": "Canyon",
            "transform": {"scale": [1, 1, 1], "rotate": [0, round(float(rng.uniform(0.0, 2.0 * math.pi)), 3), 0],
                          "translate": r3(pos)},
        })
        sec += DECOR_STEP
        side = -side
        i += 1
    return objs


# ---------------------------------------------------------------- 書き込み
def read_json(path: Path):
    raw = path.read_bytes()
    bom = raw.startswith(b"\xef\xbb\xbf")
    return json.loads(raw.decode("utf-8-sig")), bom


def write_json(path: Path, doc, bom: bool):
    text = json.dumps(doc, indent=2, ensure_ascii=False)
    with open(path, "w", encoding="utf-8-sig" if bom else "utf-8") as fp:  # 改行は Windows では CRLF
        fp.write(text)


def main():
    ap = argparse.ArgumentParser(description="谷の障害物・敵・飾りの岩を置く")
    ap.add_argument("--scene", action="store_true", help="飾りの岩（CanyonDecor_*）もシーン JSON に書く")
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    root = Path(__file__).resolve().parents[2]
    scene, scene_bom = read_json(root / SCENE_PATH)
    rail = load_rail(scene)
    walls = Walls(root)
    entries = build_entries(rail, walls)
    decor = build_decor(rail)

    for e in entries:
        where = e.get("positions", [e.get("camera_offset")])[0]
        print(f"  {e['trigger_sec']:6.1f}s  {e['enemy_type']:<8} {e['prefab']:<16} {where}"
              + (f"  rot {e['rotate_deg']}" if "rotate_deg" in e else ""))
    print(f"Wave: {len(entries)} 件（{OWN_FROM_SEC:.0f} 秒以降を作り直す） / 飾りの岩 {len(decor)} 個")
    if args.dry_run:
        return

    wave, wave_bom = read_json(root / WAVE_PATH)
    kept = [e for e in wave["spawn_entries"] if float(e.get("trigger_sec", 0.0)) < OWN_FROM_SEC]
    removed = len(wave["spawn_entries"]) - len(kept)
    wave["spawn_entries"] = kept + entries
    write_json(root / WAVE_PATH, wave, wave_bom)
    print(f"wave : {WAVE_PATH}（残す {len(kept)} 件・消す {removed} 件・足す {len(entries)} 件）")

    if args.scene:
        objs = [o for o in scene["objects"] if not str(o.get("name", "")).startswith("CanyonDecor_")]
        scene["objects"] = objs + decor
        write_json(root / SCENE_PATH, scene, scene_bom)
        print(f"scene: {SCENE_PATH}（CanyonDecor_ を {len(decor)} 個）")


if __name__ == "__main__":
    main()
