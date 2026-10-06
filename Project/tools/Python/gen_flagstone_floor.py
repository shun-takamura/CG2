"""石畳（gen_flagstone_material.py の素材）を貼った床の板を glTF で書き出す。

タイトルの水底・ボス戦の足場の置き換えと、視差オクルージョン（POM）の確認に使う。
板 1 枚なので Blender は使わず、glTF を直接書く（頂点 4 つ＋三角形 2 つ）。

マテリアル:
    baseColorTexture / normalTexture = Flagstone_BaseColor / Flagstone_NormalMap
    extras.heightTexture / extras.parallaxDepth = Flagstone_Height と深さ（cook_assets.py が .mat v4 へ）
    UV は石畳の 1 タイル（tile_m）で 1。深さは Flagstone_Height.json の実寸範囲 ÷ tile_m（UV 1 あたり）

使い方（Project/ で実行）:
    python tools/Python/gen_flagstone_floor.py               # 30m 四方で書き出して cook
    python tools/Python/gen_flagstone_floor.py --size 60     # 大きさ [m]
    python tools/Python/gen_flagstone_floor.py --depth-scale 1.5 --no-cook
出力:
    Assets/Models/FlagstoneFloor/flagstone_floor.gltf / .bin → Resources/Models/FlagstoneFloor/flagstone_floor.mesh / .mat
"""

from __future__ import annotations

import argparse
import json
import struct
import subprocess
import sys
from pathlib import Path

MATERIAL_NAME = "Flagstone"
TEX_BASE = "Textures/Terrain/Flagstone_BaseColor.png"
TEX_NORMAL = "Textures/NormalMapTexture/Terrain/Flagstone_NormalMap.png"
TEX_HEIGHT = "Textures/MaskTexture/Terrain/Flagstone_Height.png"
HEIGHT_INFO = "Textures/MaskTexture/Terrain/Flagstone_Height.json"
ROUGHNESS = 0.8   # 花崗岩のざらついた面


def build_gltf(size_m: float, tile_m: float, parallax_depth: float, rel_to_assets: str) -> tuple[dict, bytes]:
    h = size_m * 0.5
    # glTF は右手系・Y 上。上から見て反時計回り＝法線 +Y
    positions = [(-h, 0.0, -h), (h, 0.0, -h), (h, 0.0, h), (-h, 0.0, h)]
    normals = [(0.0, 1.0, 0.0)] * 4
    uvs = [((x + h) / tile_m, (z + h) / tile_m) for x, _, z in positions]
    indices = [0, 3, 2, 0, 2, 1]

    blob = bytearray()
    def append(fmt, values):
        start = len(blob)
        for v in values:
            blob.extend(struct.pack(fmt, *v) if isinstance(v, tuple) else struct.pack(fmt, v))
        return start, len(blob) - start
    pos_off, pos_len = append("<3f", positions)
    nrm_off, nrm_len = append("<3f", normals)
    uv_off, uv_len = append("<2f", uvs)
    idx_off, idx_len = append("<I", indices)

    gltf = {
        "asset": {"version": "2.0", "generator": "gen_flagstone_floor.py"},
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"mesh": 0, "name": "flagstone_floor"}],
        "meshes": [{"name": "flagstone_floor", "primitives": [{
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
            "extras": {
                "heightTexture": f"{rel_to_assets}/{TEX_HEIGHT}",
                "parallaxDepth": parallax_depth,
            },
        }],
        "textures": [{"source": 0}, {"source": 1}],
        "images": [{"uri": f"{rel_to_assets}/{TEX_BASE}"}, {"uri": f"{rel_to_assets}/{TEX_NORMAL}"}],
        "buffers": [{"uri": "flagstone_floor.bin", "byteLength": len(blob)}],
        "bufferViews": [
            {"buffer": 0, "byteOffset": pos_off, "byteLength": pos_len, "target": 34962},
            {"buffer": 0, "byteOffset": nrm_off, "byteLength": nrm_len, "target": 34962},
            {"buffer": 0, "byteOffset": uv_off, "byteLength": uv_len, "target": 34962},
            {"buffer": 0, "byteOffset": idx_off, "byteLength": idx_len, "target": 34963},
        ],
        "accessors": [
            {"bufferView": 0, "componentType": 5126, "count": 4, "type": "VEC3",
             "min": [-h, 0.0, -h], "max": [h, 0.0, h]},
            {"bufferView": 1, "componentType": 5126, "count": 4, "type": "VEC3"},
            {"bufferView": 2, "componentType": 5126, "count": 4, "type": "VEC2"},
            {"bufferView": 3, "componentType": 5125, "count": 6, "type": "SCALAR"},
        ],
    }
    return gltf, bytes(blob)


def main() -> None:
    ap = argparse.ArgumentParser(description="石畳を貼った床の板を glTF で書き出す")
    ap.add_argument("--size", type=float, default=30.0, help="板の一辺 [m]")
    ap.add_argument("--depth-scale", type=float, default=1.0, help="視差の深さの倍率（1=実寸）")
    ap.add_argument("--no-cook", action="store_true")
    args = ap.parse_args()

    project_root = Path(__file__).resolve().parents[2]
    assets = project_root / "Assets"
    info = json.loads((assets / HEIGHT_INFO).read_text(encoding="utf-8"))
    tile_m = float(info["tile_m"])
    parallax_depth = (info["height_max_m"] - info["height_min_m"]) / tile_m * args.depth_scale

    out_dir = assets / "Models" / "FlagstoneFloor"
    out_dir.mkdir(parents=True, exist_ok=True)
    gltf, blob = build_gltf(args.size, tile_m, parallax_depth, rel_to_assets="../..")
    (out_dir / "flagstone_floor.bin").write_bytes(blob)
    (out_dir / "flagstone_floor.gltf").write_text(json.dumps(gltf, indent=2), encoding="utf-8")
    print(f"[floor] {args.size}m square, tile {tile_m}m, parallaxDepth {parallax_depth:.5f} (UV units) -> {out_dir}")

    if not args.no_cook:
        result = subprocess.run([sys.executable, str(project_root / "tools" / "Python" / "cook_assets.py")],
                                cwd=str(project_root), capture_output=True, text=True)
        for line in result.stdout.splitlines():
            if "FlagstoneFloor" in line:
                print(line)
        if result.returncode != 0:
            print(result.stderr.rstrip(), file=sys.stderr)
            raise SystemExit(f"cook_assets.py failed (exit {result.returncode})")


if __name__ == "__main__":
    main()
