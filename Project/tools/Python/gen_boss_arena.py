"""ボス戦の闘技場（海の上の円い石の足場＋縁石＋柱）を glTF で書き出す。

足場の上面の石畳は BossArenaWater（薄い水の水底として石畳を描く）が受け持つ。このモデルは
  - 上面のふた（水面の円の切り口からのぞいても下が抜けないように、水底より少し下に置く）
  - 縁石（水をためる低い輪）
  - 側面（海面の下まで下りる円筒の壁）
  - 縁石の上の柱（一部は折れて低い）
だけを持つ。板が少ないので Blender は使わず、glTF を直接書く（gen_flagstone_floor.py と同じ流儀）。

座標はモデル空間（原点＝闘技場の中心・Y=0 が足場の上面＝自機の足元）。シーン JSON で
(center.x, topY, center.z) に置く。寸法は Resources/Json/Tuning/BossArenaGeometry.json に書き、
エンジン（BossStagePart / BossArenaWater）はそれを読む（手で直さない）。

使い方（Project/ で実行）:
    python tools/Python/gen_boss_arena.py            # 書き出して cook
    python tools/Python/gen_boss_arena.py --no-cook
出力:
    Assets/Models/BossArena/BossArena.gltf / .bin → Resources/Models/BossArena/BossArena.mesh / .mat
    Resources/Json/Tuning/BossArenaGeometry.json
"""

from __future__ import annotations

import argparse
import json
import math
import struct
import subprocess
import sys
from pathlib import Path

# ---------------------------------------------------------------- 配置（ワールド）
# 谷のレールの終点は (280, -60, 3505)、海岸線は Z ≈ 3530〜3710、海面 Y=-150（11 番 §3.0）
CENTER_X = 280.0
CENTER_Z = 3850.0
SEA_Y = -150.0
TOP_Y = -148.0             # 足場の上面（自機の足元）。海面の 2m 上

# ---------------------------------------------------------------- 形（モデル空間・m）
WATER_RADIUS = 36.0        # 薄い水（石畳）を描く円の半径＝縁石の内側
CURB_OUTER = 38.0          # 縁石の外側＝足場の外周
CURB_HEIGHT = 0.4          # 縁石の高さ（水をためる）
SIDE_BOTTOM = -18.0        # 側面が下りる深さ（海面の下まで）
SIDE_BOTTOM_FLARE = 2.5    # 底へ向かって広がる量（土台らしく）
CAP_Y = -0.08              # 上面のふた（石畳の水底 0 の少し下）
PLAYER_RADIUS = 32.0       # 自機が動ける半径
CAMERA_RADIUS = 34.5       # カメラが出られる水平の半径（柱の内側）

PILLAR_COUNT = 12
PILLAR_RING = 37.0         # 柱の中心の半径（縁石の上）
PILLAR_RADIUS = 1.0        # 八角柱の外接半径
PILLAR_SIDES = 8
PLINTH_RADIUS = 1.45       # 台座
PLINTH_HEIGHT = 0.7
CAPITAL_RADIUS = 1.4       # 柱頭（折れていない柱だけ）
CAPITAL_HEIGHT = 0.6
# 柱の高さ（縁石の上から）。0 番は +Z（ボスの側）。遺跡らしく一部は折れて低い
PILLAR_HEIGHTS = [14.0, 14.0, 7.5, 14.0, 14.0, 4.0, 14.0, 11.0, 14.0, 14.0, 6.0, 14.0]
PILLAR_FULL = 14.0

SEGMENTS = 96              # 円の分割
TILE_M = 3.0               # 石のテクスチャ 1 枚の長さ（石畳と同じ）

MATERIAL_NAME = "ArenaStone"
TEX_BASE = "Textures/Terrain/Flagstone_BaseColor.png"
TEX_NORMAL = "Textures/NormalMapTexture/Terrain/Flagstone_NormalMap.png"
ROUGHNESS = 0.85


class Mesh:
    def __init__(self) -> None:
        self.pos: list[tuple[float, float, float]] = []
        self.nrm: list[tuple[float, float, float]] = []
        self.uv: list[tuple[float, float]] = []
        self.idx: list[int] = []

    def quad(self, a, b, c, d, n_a, n_b, n_c, n_d, uv_a, uv_b, uv_c, uv_d) -> None:
        """a→b→c→d を表から見て反時計回りで渡す（glTF の表）"""
        base = len(self.pos)
        self.pos += [a, b, c, d]
        self.nrm += [n_a, n_b, n_c, n_d]
        self.uv += [uv_a, uv_b, uv_c, uv_d]
        self.idx += [base, base + 1, base + 2, base, base + 2, base + 3]

    def disk(self, y: float, r_in: float, r_out: float, up: bool, segs: int, cx=0.0, cz=0.0) -> None:
        """水平な輪（r_in=0 で円板）。up=True なら表が +Y"""
        n = (0.0, 1.0 if up else -1.0, 0.0)
        for i in range(segs):
            t0 = 2.0 * math.pi * i / segs
            t1 = 2.0 * math.pi * (i + 1) / segs
            p = [
                (cx + r_out * math.sin(t0), y, cz + r_out * math.cos(t0)),
                (cx + r_out * math.sin(t1), y, cz + r_out * math.cos(t1)),
                (cx + r_in * math.sin(t1), y, cz + r_in * math.cos(t1)),
                (cx + r_in * math.sin(t0), y, cz + r_in * math.cos(t0)),
            ]
            uvs = [(q[0] / TILE_M, q[2] / TILE_M) for q in p]
            # 上から見て反時計回り（+Y が表）＝外 t0 → 外 t1 → 内 t1 → 内 t0
            order = [0, 1, 2, 3] if up else [1, 0, 3, 2]
            self.quad(*[p[k] for k in order], n, n, n, n, *[uvs[k] for k in order])

    def tube(self, y0: float, r0: float, y1: float, r1: float, outward: bool, segs: int,
             cx=0.0, cz=0.0, smooth=True, phase=0.0) -> None:
        """y0（下・半径 r0）→ y1（上・半径 r1）の側面。outward=True で表が外"""
        slope = (r0 - r1) / max(y1 - y0, 1e-6)
        for i in range(segs):
            t0 = phase + 2.0 * math.pi * i / segs
            t1 = phase + 2.0 * math.pi * (i + 1) / segs
            def normal(t):
                nx, nz = math.sin(t), math.cos(t)
                l = math.sqrt(1.0 + slope * slope)
                n = (nx / l, slope / l, nz / l)
                return n if outward else (-n[0], -n[1], -n[2])
            if smooth:
                n0, n1 = normal(t0), normal(t1)
            else:
                n0 = n1 = normal((t0 + t1) * 0.5)
            arc0 = r1 * t0 / TILE_M
            arc1 = r1 * t1 / TILE_M
            b0 = (cx + r0 * math.sin(t0), y0, cz + r0 * math.cos(t0))
            b1 = (cx + r0 * math.sin(t1), y0, cz + r0 * math.cos(t1))
            u0 = (cx + r1 * math.sin(t0), y1, cz + r1 * math.cos(t0))
            u1 = (cx + r1 * math.sin(t1), y1, cz + r1 * math.cos(t1))
            v0, v1 = -y0 / TILE_M, -y1 / TILE_M
            if outward:
                # 外から見て反時計回り：下 t0 → 下 t1 → 上 t1 → 上 t0
                self.quad(b0, b1, u1, u0, n0, n1, n1, n0,
                          (arc0, v0), (arc1, v0), (arc1, v1), (arc0, v1))
            else:
                self.quad(b1, b0, u0, u1, n1, n0, n0, n1,
                          (arc1, v0), (arc0, v0), (arc0, v1), (arc1, v1))


def build_mesh() -> Mesh:
    m = Mesh()
    # 上面のふた（水の切り口からのぞいても海が見えないように）
    m.disk(CAP_Y, 0.0, WATER_RADIUS + 0.05, True, SEGMENTS)
    # 縁石：内壁・上面・（外壁は側面とつなげる）
    m.tube(CAP_Y, WATER_RADIUS, CURB_HEIGHT, WATER_RADIUS, False, SEGMENTS)
    m.disk(CURB_HEIGHT, WATER_RADIUS, CURB_OUTER, True, SEGMENTS)
    # 側面：縁石の上から海の下まで
    m.tube(SIDE_BOTTOM, CURB_OUTER + SIDE_BOTTOM_FLARE, CURB_HEIGHT, CURB_OUTER, True, SEGMENTS)

    # 柱
    for i, h in enumerate(PILLAR_HEIGHTS[:PILLAR_COUNT]):
        t = 2.0 * math.pi * i / PILLAR_COUNT
        cx, cz = PILLAR_RING * math.sin(t), PILLAR_RING * math.cos(t)
        base = CURB_HEIGHT
        # 台座（四角に近い八角形）
        m.tube(base, PLINTH_RADIUS, base + PLINTH_HEIGHT, PLINTH_RADIUS, True, PILLAR_SIDES, cx, cz,
               smooth=False, phase=math.pi / PILLAR_SIDES)
        m.disk(base + PLINTH_HEIGHT, PILLAR_RADIUS, PLINTH_RADIUS, True, PILLAR_SIDES, cx, cz)
        # 胴（上へ少し細く）
        top = base + h
        m.tube(base + PLINTH_HEIGHT, PILLAR_RADIUS, top, PILLAR_RADIUS * 0.9, True, PILLAR_SIDES, cx, cz,
               smooth=False, phase=math.pi / PILLAR_SIDES)
        if h >= PILLAR_FULL - 1e-3:
            # 柱頭
            m.disk(top, PILLAR_RADIUS * 0.9, CAPITAL_RADIUS, False, PILLAR_SIDES, cx, cz)
            m.tube(top, CAPITAL_RADIUS, top + CAPITAL_HEIGHT, CAPITAL_RADIUS, True, PILLAR_SIDES, cx, cz,
                   smooth=False, phase=math.pi / PILLAR_SIDES)
            m.disk(top + CAPITAL_HEIGHT, 0.0, CAPITAL_RADIUS, True, PILLAR_SIDES, cx, cz)
        else:
            # 折れた柱の切り口
            m.disk(top, 0.0, PILLAR_RADIUS * 0.9, True, PILLAR_SIDES, cx, cz)
    return m


def build_gltf(m: Mesh, rel_to_assets: str) -> tuple[dict, bytes]:
    blob = bytearray()

    def append(fmt, values):
        while len(blob) % 4:
            blob.append(0)
        start = len(blob)
        for v in values:
            blob.extend(struct.pack(fmt, *v) if isinstance(v, tuple) else struct.pack(fmt, v))
        return start, len(blob) - start

    pos_off, pos_len = append("<3f", m.pos)
    nrm_off, nrm_len = append("<3f", m.nrm)
    uv_off, uv_len = append("<2f", m.uv)
    idx_off, idx_len = append("<I", m.idx)
    mins = [min(p[k] for p in m.pos) for k in range(3)]
    maxs = [max(p[k] for p in m.pos) for k in range(3)]

    gltf = {
        "asset": {"version": "2.0", "generator": "gen_boss_arena.py"},
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"mesh": 0, "name": "BossArena"}],
        "meshes": [{"name": "BossArena", "primitives": [{
            "attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2},
            "indices": 3, "material": 0}]}],
        "materials": [{
            "name": MATERIAL_NAME,
            "pbrMetallicRoughness": {
                "baseColorTexture": {"index": 0},
                "metallicFactor": 0.0,
                "roughnessFactor": ROUGHNESS,
            },
            "normalTexture": {"index": 1},
        }],
        "textures": [{"source": 0}, {"source": 1}],
        "images": [{"uri": f"{rel_to_assets}/{TEX_BASE}"}, {"uri": f"{rel_to_assets}/{TEX_NORMAL}"}],
        "buffers": [{"uri": "BossArena.bin", "byteLength": len(blob)}],
        "bufferViews": [
            {"buffer": 0, "byteOffset": pos_off, "byteLength": pos_len, "target": 34962},
            {"buffer": 0, "byteOffset": nrm_off, "byteLength": nrm_len, "target": 34962},
            {"buffer": 0, "byteOffset": uv_off, "byteLength": uv_len, "target": 34962},
            {"buffer": 0, "byteOffset": idx_off, "byteLength": idx_len, "target": 34963},
        ],
        "accessors": [
            {"bufferView": 0, "componentType": 5126, "count": len(m.pos), "type": "VEC3",
             "min": mins, "max": maxs},
            {"bufferView": 1, "componentType": 5126, "count": len(m.nrm), "type": "VEC3"},
            {"bufferView": 2, "componentType": 5126, "count": len(m.uv), "type": "VEC2"},
            {"bufferView": 3, "componentType": 5125, "count": len(m.idx), "type": "SCALAR"},
        ],
    }
    return gltf, bytes(blob)


def write_geometry_json(path: Path) -> None:
    doc = {
        "_comment": "gen_boss_arena.py が書く。手で直さない（直すならスクリプトの定数を直して流し直す）",
        "center": [CENTER_X, TOP_Y, CENTER_Z],
        "seaY": SEA_Y,
        "waterRadius": WATER_RADIUS,
        "curbOuter": CURB_OUTER,
        "curbHeight": CURB_HEIGHT,
        "playerRadius": PLAYER_RADIUS,
        "cameraRadius": CAMERA_RADIUS,
        "pillarRing": PILLAR_RING,
        "pillarCount": PILLAR_COUNT,
    }
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(doc, indent=2, ensure_ascii=False), encoding="utf-8")


def main() -> None:
    ap = argparse.ArgumentParser(description="ボス戦の闘技場を glTF で書き出す")
    ap.add_argument("--no-cook", action="store_true")
    args = ap.parse_args()

    project_root = Path(__file__).resolve().parents[2]
    assets = project_root / "Assets"
    out_dir = assets / "Models" / "BossArena"
    out_dir.mkdir(parents=True, exist_ok=True)

    mesh = build_mesh()
    gltf, blob = build_gltf(mesh, rel_to_assets="../..")
    (out_dir / "BossArena.bin").write_bytes(blob)
    (out_dir / "BossArena.gltf").write_text(json.dumps(gltf, indent=2), encoding="utf-8")
    geo = project_root / "Resources" / "Json" / "Tuning" / "BossArenaGeometry.json"
    write_geometry_json(geo)
    print(f"[arena] {len(mesh.pos)} verts, {len(mesh.idx) // 3} tris -> {out_dir}")
    print(f"[arena] geometry -> {geo}")

    if not args.no_cook:
        result = subprocess.run([sys.executable, str(project_root / "tools" / "Python" / "cook_assets.py")],
                                cwd=str(project_root), capture_output=True, text=True)
        for line in result.stdout.splitlines():
            if "BossArena" in line:
                print(line)
        if result.returncode != 0:
            print(result.stderr.rstrip(), file=sys.stderr)
            raise SystemExit(f"cook_assets.py failed (exit {result.returncode})")


if __name__ == "__main__":
    main()
