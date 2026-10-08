#!/usr/bin/env python3
"""地形シェーダ（Terrain.PS.hlsl）の層の素材（草・土・岩）を作り、配列テクスチャの DDS にまとめる。

設計書: Documents/Tasks/12_TerrainRendering.md §3.2。

    - 各層の模様は gen_terrain_material.py の関数で作る（タイリングする FBM の高さ → 法線・色）。
    - 色の A に高さ（0..1）を入れる。地形シェーダの高さブレンドがこれで境界を噛み合わせる。
    - 各層を texconv で BC7 にし、同じ大きさ・同じミップ数の DDS を 1 つの配列 DDS（DX10 ヘッダ）へ結合する。
      texassemble が無いので結合は自前。DDS の配列は「要素ごとに全ミップが並ぶ」ので、データ部を順に繋げばよい。
    - 層の順番はスプラットマップのチャンネルと同じ（0 = 草 / R、1 = 土 / G、2 = 岩 / B）。
    - "type": "voronoi" の層（岩）は、ボロノイの塊で作る：塊ごとに傾いた面と色を持たせ、塊の境を割れ目にする。
      FBM だけだと雲状のふわっとした模様になり、岩の「角ばった塊」に見えないため。

使い方（Project/ で実行）:
    python tools/Python/gen_terrain_layers.py --recipe tools/Python/recipes/terrain_layers.json

出力:
    Resources/Textures/Terrain/<name>_Color.dds                     BC7_UNORM_SRGB の配列（A = 高さ）
    Resources/Textures/NormalMapTexture/Terrain/<name>_Normal.dds   BC7_UNORM の配列
    Generated/Terrain/<name>/<層>_Color.png / _Normal.png           中間の PNG（Assets/ に置くと cook が上書きするので置かない）
"""

from __future__ import annotations

import argparse
import json
import math
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np
from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))
import gen_terrain_material as material  # noqa: E402

TEXCONV = Path("externals/texconv/Texconv.exe")
COLOR_OUT = Path("Resources/Textures/Terrain")
NORMAL_OUT = Path("Resources/Textures/NormalMapTexture/Terrain")

DDS_HEADER_SIZE = 4 + 124            # "DDS " + DDS_HEADER
DX10_HEADER_SIZE = 20                # DDS_HEADER_DXT10
DX10_ARRAY_SIZE_OFFSET = DDS_HEADER_SIZE + 12


def voronoi_tileable(px: np.ndarray, py: np.ndarray, cells: int, rng: np.random.Generator, chunk: int = 16):
    """タイリングするボロノイ。px, py（UV。歪ませてあってよい）ごとに、最寄りの点の番号・その点への相対位置・
    1 番目と 2 番目の距離を返す。点を上下左右に複製して境界をまたぐ"""
    pts = rng.random((cells, 2))
    offs = np.array([(dx, dy) for dy in (-1, 0, 1) for dx in (-1, 0, 1)], dtype=np.float64)
    all_pts = (pts[None, :, :] + offs[:, None, :]).reshape(-1, 2).astype(np.float32)
    owner = np.tile(np.arange(cells), 9)

    size_y = px.shape[0]
    idx = np.empty(px.shape, dtype=np.int64)
    rel = np.empty(px.shape + (2,))
    d1 = np.empty(px.shape)
    d2 = np.empty(px.shape)
    for y0 in range(0, size_y, chunk):
        qx = (px[y0:y0 + chunk] % 1.0).astype(np.float32)
        qy = (py[y0:y0 + chunk] % 1.0).astype(np.float32)
        dx = qx[..., None] - all_pts[None, None, :, 0]
        dy = qy[..., None] - all_pts[None, None, :, 1]
        dist = np.sqrt(dx * dx + dy * dy)
        part = np.argpartition(dist, 1, axis=-1)[..., :2]
        a = np.take_along_axis(dist, part[..., :1], axis=-1)[..., 0]
        b = np.take_along_axis(dist, part[..., 1:2], axis=-1)[..., 0]
        first = np.where(a <= b, part[..., 0], part[..., 1])
        idx[y0:y0 + chunk] = owner[first]
        rel[y0:y0 + chunk, :, 0] = np.take_along_axis(dx, first[..., None], axis=-1)[..., 0]
        rel[y0:y0 + chunk, :, 1] = np.take_along_axis(dy, first[..., None], axis=-1)[..., 0]
        d1[y0:y0 + chunk] = np.minimum(a, b)
        d2[y0:y0 + chunk] = np.maximum(a, b)
    return idx, rel, d1, d2


def tileable_fbm(size: int, scale: float, octaves: int, seed: int) -> np.ndarray:
    return material.generate_heightfield({"size": size, "seed": seed, "scale": scale,
                                          "octaves": octaves, "persistence": 0.5, "lacunarity": 2.0})


def voronoi_rock(layer: dict, size: int) -> tuple[np.ndarray, np.ndarray]:
    """角ばった岩の塊。高さ（0..1）と色（RGB 0..1）を返す。
    座標を歪ませてから分割するので、塊の境は直線ではなくうねった割れ方になる。
    高さは「塊の面 − 割れ目の深さ」。割れ目の幅はノイズで揺らし、ところどころは割れない"""
    seed = int(layer["seed"])
    rng = np.random.default_rng(seed)
    coords = (np.arange(size) + 0.5) / size
    py0, px0 = np.meshgrid(coords, coords, indexing="ij")
    warp = layer["warp"]
    wx = (tileable_fbm(size, layer["warp_scale"], 4, seed + 11) - 0.5) * 2.0 * warp
    wy = (tileable_fbm(size, layer["warp_scale"], 4, seed + 12) - 0.5) * 2.0 * warp
    px, py = px0 + wx, py0 + wy

    gaps = tileable_fbm(size, layer["gap_scale"], 3, seed + 13)   # 割れ目の幅の揺れ（0..1）
    height = np.zeros((size, size))
    darken = np.ones((size, size))
    color_t = np.zeros((size, size))
    for k, level in enumerate(layer["levels"]):
        cells = int(level["cells"])
        cell_size = 1.0 / math.sqrt(cells)
        idx, rel, d1, d2 = voronoi_tileable(px, py, cells, rng)
        base = rng.uniform(0.0, 1.0, cells)
        tilt = rng.normal(0.0, level["tilt"], (cells, 2))
        tone = rng.random(cells)
        facet = base[idx] * level["step"] + (tilt[idx, 0] * rel[..., 0] + tilt[idx, 1] * rel[..., 1]) / cell_size
        # 割れ目：塊の境からの距離 (d2 - d1)。幅をノイズで 0 〜 crack 倍に揺らす（0 の所は割れない）
        width = level["crack"] * cell_size * np.clip((gaps - level["gap_cut"]) / (1.0 - level["gap_cut"]), 0.0, 1.0)
        open_ = 1.0 - smoothstep_np(0.0, 1.0, (d2 - d1) / np.maximum(width, 1e-6))
        open_ = np.where(width > 1e-6, open_, 0.0)
        height += level["weight"] * facet - level["crack_depth"] * open_
        darken *= 1.0 - level["crack_darken"] * open_
        if k == 0:
            color_t = tone[idx]

    grain = tileable_fbm(size, layer["grain_scale"], 6, seed + 1)
    height += layer["grain"] * grain
    height = (height - height.min()) / max(height.max() - height.min(), 1e-9)

    palette = np.array(layer["palette"], dtype=np.float64)
    pick = np.clip(color_t * (len(palette) - 1), 0.0, len(palette) - 1 - 1e-6)
    i0 = np.floor(pick).astype(int)
    f = (pick - i0)[..., None]
    rgb = palette[i0] * (1.0 - f) + palette[i0 + 1] * f
    rgb *= darken[..., None]
    rgb *= (0.8 + 0.35 * height)[..., None]
    mottle = tileable_fbm(size, layer["grain_scale"] * 0.25, 4, seed + 2)
    rgb *= (1.0 + layer["color_jitter"] * (mottle - 0.5) * 2.0)[..., None]
    return height, np.clip(rgb, 0.0, 1.0)


def smoothstep_np(a: float, b: float, x: np.ndarray) -> np.ndarray:
    t = np.clip((x - a) / max(b - a, 1e-9), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def build_layer(layer: dict, size: int) -> tuple[Image.Image, Image.Image]:
    if layer.get("type") == "voronoi":
        height, rgb = voronoi_rock(layer, size)
        normal_img, _ = material.heightfield_to_normal_map(height, float(layer["bump_strength"]))
        color_img = Image.fromarray(np.clip(rgb * 255.0 + 0.5, 0.0, 255.0).astype(np.uint8), "RGB")
        alpha = Image.fromarray(np.clip(height * 255.0 + 0.5, 0.0, 255.0).astype(np.uint8), "L")
        color_img.putalpha(alpha)
        return color_img, normal_img

    recipe = json.loads(json.dumps(material.DEFAULT_RECIPE))
    recipe.update(layer)
    recipe["size"] = size
    height = material.generate_heightfield(recipe)
    normal_img, slope = material.heightfield_to_normal_map(height, float(recipe["bump_strength"]))
    color_img = material.heightfield_to_base_color(height, slope, recipe)
    alpha = Image.fromarray(np.clip(height * 255.0 + 0.5, 0.0, 255.0).astype(np.uint8), "L")
    color_img.putalpha(alpha)
    return color_img, normal_img


def texconv(png: Path, out_dir: Path, fmt: str) -> Path:
    cmd = [str(Path(__file__).resolve().parents[2] / TEXCONV), "-nologo", "-y", "-f", fmt, "-bc", "x", "-dx10", "-o", str(out_dir), str(png)]
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        raise SystemExit(f"texconv failed: {png}\n{result.stdout}\n{result.stderr}")
    return out_dir / (png.stem + ".dds")


def combine_array(parts: list[Path], out_path: Path) -> None:
    """同じ形式・大きさ・ミップ数の DDS（DX10 ヘッダ付き）を、配列の DDS に結合する。"""
    blobs = [p.read_bytes() for p in parts]
    head_len = DDS_HEADER_SIZE + DX10_HEADER_SIZE
    for p, b in zip(parts, blobs):
        if b[:4] != b"DDS " or b[84:88] != b"DX10":
            raise SystemExit(f"DX10 ヘッダの DDS ではない: {p}")
        if b[4:head_len] != blobs[0][4:head_len]:
            raise SystemExit(f"大きさ・形式・ミップ数が揃っていない: {p}")
        if struct.unpack_from("<I", b, DX10_ARRAY_SIZE_OFFSET)[0] != 1:
            raise SystemExit(f"すでに配列になっている: {p}")
    header = bytearray(blobs[0][:head_len])
    struct.pack_into("<I", header, DX10_ARRAY_SIZE_OFFSET, len(blobs))
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_bytes(bytes(header) + b"".join(b[head_len:] for b in blobs))


def preview_only(recipe: dict, layer_name: str, work: Path) -> None:
    """1 層だけ作って PNG を見る（DDS は作らない）。色と、真上から光を当てた陰影を並べる"""
    layer = next(l for l in recipe["layers"] if l["name"] == layer_name)
    color_img, normal_img = build_layer(layer, int(recipe["size"]))
    n = np.asarray(normal_img, dtype=np.float64) / 255.0 * 2.0 - 1.0
    light = np.array([-0.5, 0.5, 0.7])
    light /= np.linalg.norm(light)
    shade = np.clip(n @ light, 0.0, 1.0)
    rgb = np.asarray(color_img.convert("RGB"), dtype=np.float64) / 255.0
    lit = np.clip(rgb * (0.25 + 0.95 * shade[..., None]), 0.0, 1.0)
    out = Image.new("RGB", (color_img.width * 2, color_img.height))
    out.paste(color_img.convert("RGB"), (0, 0))
    out.paste(Image.fromarray((lit * 255).astype(np.uint8)), (color_img.width, 0))
    path = work / f"{layer_name}_Preview.png"
    out.save(path)
    print(f"[preview] {path}  平均の傾き nz={float(np.mean(n[..., 2])):.3f}")


def main() -> None:
    ap = argparse.ArgumentParser(description="地形の層の素材を作り、配列 DDS にまとめる（12_TerrainRendering.md §3.2）")
    ap.add_argument("--recipe", type=Path, required=True)
    ap.add_argument("--preview", help="この名前の層だけ作って PNG で確認する（DDS は作らない）")
    args = ap.parse_args()

    project_root = Path(__file__).resolve().parents[2]
    recipe = json.loads(args.recipe.read_text(encoding="utf-8"))
    name = recipe["name"]
    size = int(recipe["size"])
    work = project_root / "Generated" / "Terrain" / name
    work.mkdir(parents=True, exist_ok=True)
    if args.preview:
        preview_only(recipe, args.preview, work)
        return

    color_dds, normal_dds = [], []
    with tempfile.TemporaryDirectory() as tmp:
        tmp_dir = Path(tmp)
        for layer in recipe["layers"]:
            color_img, normal_img = build_layer(layer, size)
            color_png = work / f"{layer['name']}_Color.png"
            normal_png = work / f"{layer['name']}_Normal.png"
            color_img.save(color_png)
            normal_img.save(normal_png)
            color_dds.append(texconv(color_png, tmp_dir, "BC7_UNORM_SRGB"))
            normal_dds.append(texconv(normal_png, tmp_dir, "BC7_UNORM"))
            print(f"[layer] {layer['name']}")

        color_out = project_root / COLOR_OUT / f"{name}_Color.dds"
        normal_out = project_root / NORMAL_OUT / f"{name}_Normal.dds"
        combine_array(color_dds, color_out)
        combine_array(normal_dds, normal_out)

    print(f"[array] {color_out.relative_to(project_root)}  ({len(color_dds)} 層)")
    print(f"[array] {normal_out.relative_to(project_root)}  ({len(normal_dds)} 層)")


if __name__ == "__main__":
    main()
