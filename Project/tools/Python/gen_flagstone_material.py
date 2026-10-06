"""乱形石張り（クレイジーペービング）の石畳の素材を、ハイトマップから手続き的に生成する。

タイトルの水底とボス戦の足場で共通に使う（5_TitleScene.md / 10_WaterReflection.md）。
参考：辺のまっすぐな不揃いの多角形の石（花崗岩っぽいごま塩肌、一部に錆色の染み）を、
小石入りのコンクリートの太めの目地で張ったもの。

設計:
    - まず「実寸（メートル）」のハイトフィールドを作り、そこから全部を導出する。
        石の形   : ユークリッド距離のボロノイ（各セル＝辺がまっすぐな凸多角形）。縁までの距離は
                   隣のセルとの二等分線までの正確な距離で求める。石ごとに目地の幅を変える。
        石の高さ : 石ごとの高さの差と傾き、縁の小さな面取りと欠け、花崗岩のざらつき。
        目地     : 一段沈め、細かいボロノイで作った小石（粒ごとに丸く盛り上がる）を詰める。
    - 法線マップは実寸の勾配から計算する（エンコードは gen_terrain_material.py と同じ：行0=上、ny=-dy）。
    - ベースカラーには陰影を描き込まない。石の色・ごま塩の粒・錆の染み・小石の色だけ。
    - ハイトマップも出力する（視差オクルージョン用）。範囲は <name>_Height.json に実寸で残す。
    - ノイズは FFT で周波数整形、ボロノイはトーラス上で計算＝上下左右とも継ぎ目なくタイルできる。

使い方（Project/ で実行）:
    python tools/Python/gen_flagstone_material.py                     # 生成＋クック
    python tools/Python/gen_flagstone_material.py --preview <フォルダ>  # 確認用の陰影付き画像も出す
    python tools/Python/gen_flagstone_material.py --no-cook --seed 3

出力:
    Assets/Textures/Terrain/<name>_BaseColor.png
    Assets/Textures/NormalMapTexture/Terrain/<name>_NormalMap.png   （"NormalMap" を含む＝線形で圧縮）
    Assets/Textures/MaskTexture/Terrain/<name>_Height.png           （"MaskTexture" を含む＝線形で圧縮）
    Assets/Textures/MaskTexture/Terrain/<name>_Height.json          （高さの実寸の範囲）
"""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
from pathlib import Path

import numpy as np
from PIL import Image

# ============================================================
# レシピ（単位はメートル）
# ============================================================
RECIPE = {
    "name": "Flagstone",
    "seed": 5,
    "size": 2048,                    # テクスチャの解像度（1タイル）
    "tile_m": 3.0,                   # 1タイルの実寸（小石の粒まで描くため 3m）
    # 石の形
    "stone_cells": 7,                # 1タイルあたりの格子数（石の平均の大きさ＝tile_m/stone_cells）
    "stone_jitter": 0.45,            # 格子の中での石の中心のばらつき（0..0.5）
    "split_chance": 0.25,            # 石を直線で2つに割る確率（細長い石・三角の石を混ぜる）
    "grout_half_m": (0.008, 0.016),  # 目地の半幅（石ごと）
    "edge_jag_m": 0.003,             # 縁のガタつき
    "bevel_m": (0.004, 0.010),       # 縁の面取りの幅（角は鋭め）
    "bevel_depth_m": 0.004,
    "chip_m": 0.02,                  # 縁の欠けの大きさ
    "chip_amount": 0.35,
    "stone_offset_m": 0.004,         # 石ごとの高さの差
    "stone_tilt": 0.008,             # 石ごとの傾き（勾配）
    "surface_m": 0.0015,             # 表面のうねり
    "grain_m": 0.0016,               # 花崗岩のざらつき
    # 目地（小石入りのコンクリート）
    "grout_depth_m": 0.012,          # 目地の沈み
    "pebble_cells": 300,             # 1タイルあたりの小石の格子数（粒の大きさ≒tile_m/pebble_cells）
    "pebble_height_m": 0.003,
    # 色（sRGB 0..1）
    "stone_gray_range": (0.46, 0.78),  # 石ごとの明るさ
    "stone_tint": [(1.00, 1.00, 1.00), (1.02, 1.00, 0.97), (0.99, 0.99, 1.00), (1.03, 1.01, 0.96)],
    "speckle_dark": 0.5,             # 黒い粒の量
    "speckle_light": 0.3,            # 白い粒の量
    "rust_color": (0.68, 0.57, 0.38),  # 黄土色
    "rust_stone_chance": 0.3,        # 錆色の染みが出る石の割合
    "rust_amount": 0.55,
    "pebble_colors": [(0.20, 0.20, 0.20), (0.32, 0.31, 0.29), (0.45, 0.42, 0.38),
                      (0.55, 0.52, 0.47), (0.38, 0.33, 0.27), (0.62, 0.60, 0.56)],
    "cement_color": (0.40, 0.38, 0.34),
}


# ============================================================
# ノイズ（FFT で周波数整形＝周期的＝タイル可能）
# ============================================================
def tileable_noise(rng: np.random.Generator, n: int, beta: float, cutoff: float = 0.0) -> np.ndarray:
    """1/f^beta のノイズ。cutoff（周期/画素）未満の低周波は落とす。0..1 に正規化"""
    white = rng.standard_normal((n, n))
    fx = np.fft.fftfreq(n)[None, :]
    fy = np.fft.fftfreq(n)[:, None]
    r = np.sqrt(fx * fx + fy * fy)
    r[0, 0] = 1.0
    spec = np.fft.fft2(white) / (r ** beta)
    spec[r < cutoff] = 0.0
    spec[0, 0] = 0.0
    out = np.real(np.fft.ifft2(spec))
    return (out - out.min()) / (out.max() - out.min())


def smoothstep(e0, e1, x):
    t = np.clip((x - e0) / (e1 - e0), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


# ============================================================
# ボロノイ（トーラス上＝タイル可能）
# ============================================================
def voronoi(n, g, jitter, rng, want_edge=True):
    """ユークリッド距離のボロノイ。戻り値: (セル番号, 縁＝二等分線までの距離 [格子単位], 中心からの位置 x, y [格子単位])"""
    cell_px = n / g
    y, x = np.mgrid[0:n, 0:n].astype(np.float32)
    cu = x / cell_px
    cv = y / cell_px
    ci = np.floor(cu).astype(np.int32)
    cj = np.floor(cv).astype(np.int32)
    seeds = (0.5 + rng.uniform(-jitter, jitter, (g, g, 2))).astype(np.float32)

    # 1回目：最も近い種
    best_d = np.full((n, n), np.inf, dtype=np.float32)
    best_id = np.zeros((n, n), dtype=np.int32)
    best_sx = np.zeros((n, n), dtype=np.float32)   # 最も近い種の位置（折り返しを解いた座標）
    best_sy = np.zeros((n, n), dtype=np.float32)
    offsets = [(ox, oy) for oy in range(-2, 3) for ox in range(-2, 3)]
    for ox, oy in offsets:
        ni, nj = ci + ox, cj + oy
        wi, wj = ni % g, nj % g
        sx = ni + seeds[wj, wi, 0]
        sy = nj + seeds[wj, wi, 1]
        d = (cu - sx) ** 2 + (cv - sy) ** 2
        closer = d < best_d
        best_d = np.where(closer, d, best_d)
        best_id = np.where(closer, wj * g + wi, best_id)
        best_sx = np.where(closer, sx, best_sx)
        best_sy = np.where(closer, sy, best_sy)
    if not want_edge:
        return best_id, None, cu - best_sx, cv - best_sy

    # 2回目：最も近い種と他の種の二等分線までの距離の最小（＝縁までの正確な距離）
    edge = np.full((n, n), np.inf, dtype=np.float32)
    for ox, oy in offsets:
        ni, nj = ci + ox, cj + oy
        wi, wj = ni % g, nj % g
        sx = ni + seeds[wj, wi, 0]
        sy = nj + seeds[wj, wi, 1]
        dx, dy = sx - best_sx, sy - best_sy
        length = np.sqrt(dx * dx + dy * dy)
        same = length < 1e-5
        mx, my = (sx + best_sx) * 0.5, (sy + best_sy) * 0.5
        d = ((mx - cu) * dx + (my - cv) * dy) / np.where(same, 1.0, length)
        edge = np.where(same, edge, np.minimum(edge, d))
    return best_id, edge, cu - best_sx, cv - best_sy


# ============================================================
# 生成
# ============================================================
def generate(recipe):
    n = recipe["size"]
    tile = recipe["tile_m"]
    px = tile / n
    rng = np.random.default_rng(recipe["seed"])

    # ---- 石 ----
    g = recipe["stone_cells"]
    cell_m = tile / g
    stone_id, edge_cells, lx, ly = voronoi(n, g, recipe["stone_jitter"], rng)
    edge_m = edge_cells * cell_m
    local_x, local_y = lx * cell_m, ly * cell_m
    count = g * g

    # 一部の石を、中心の近くを通るランダムな直線で2つに割る（ボロノイの六角形っぽさを崩す）。
    # 割った片側は別の石として番号を振り直し、縁までの距離は直線までの距離とも比べる
    split = rng.random(count) < recipe["split_chance"]
    cut_angle = rng.uniform(0.0, np.pi, count)
    cut_offset = rng.uniform(-0.12, 0.12, count) * cell_m
    side = (local_x * np.cos(cut_angle[stone_id]) + local_y * np.sin(cut_angle[stone_id])
            - cut_offset[stone_id])
    is_split = split[stone_id]
    edge_m = np.where(is_split, np.minimum(edge_m, np.abs(side)), edge_m)
    stone_id = np.where(is_split & (side < 0.0), stone_id + count, stone_id)
    count *= 2

    s_grout = rng.uniform(*recipe["grout_half_m"], count)
    s_offset = rng.uniform(-1.0, 1.0, count) * recipe["stone_offset_m"]
    s_tilt_x = rng.uniform(-1.0, 1.0, count) * recipe["stone_tilt"]
    s_tilt_y = rng.uniform(-1.0, 1.0, count) * recipe["stone_tilt"]
    s_bevel = rng.uniform(*recipe["bevel_m"], count)
    s_gray = rng.uniform(*recipe["stone_gray_range"], count)
    tints = np.array(recipe["stone_tint"])
    s_tint = tints[rng.integers(0, len(tints), count)]
    s_rust = rng.random(count) < recipe["rust_stone_chance"]
    s_band = rng.random(count) < 0.07            # 黄土色の帯が通る石（参考画像の帯）
    s_band_angle = rng.uniform(0, np.pi, count)

    jag = (tileable_noise(rng, n, 1.8, cutoff=20.0 / n) - 0.5) * 2.0 * recipe["edge_jag_m"]
    chip_n = tileable_noise(rng, n, 1.7, cutoff=14.0 / n)
    amount = recipe["chip_amount"]
    chip = np.clip((chip_n - (1.0 - amount)) / max(amount, 1e-3), 0.0, 1.0) ** 1.5
    d_in = edge_m - s_grout[stone_id] - jag - chip * recipe["chip_m"]   # 石の内側の縁までの距離 [m]
    stone_mask = smoothstep(-px, px, d_in)

    bevel = smoothstep(0.0, 1.0, np.clip(d_in / s_bevel[stone_id], 0.0, 1.0))
    swell = tileable_noise(rng, n, 2.2, cutoff=4.0 / n)
    grain = (tileable_noise(rng, n, 0.6, cutoff=200.0 / n) * 0.6
             + tileable_noise(rng, n, 1.2, cutoff=60.0 / n) * 0.4)    # 細かい粒＋少し大きい粒立ち
    tilt = s_tilt_x[stone_id] * local_x + s_tilt_y[stone_id] * local_y
    h_stone = (s_offset[stone_id] + tilt
               - (1.0 - bevel) * recipe["bevel_depth_m"]
               + (swell - 0.5) * 2.0 * recipe["surface_m"]
               + (grain - 0.5) * 2.0 * recipe["grain_m"])

    # ---- 目地の小石 ----
    pg = recipe["pebble_cells"]
    peb_id, _, plx, ply = voronoi(n, pg, 0.42, rng, want_edge=False)
    peb_count = pg * pg
    peb_r = np.sqrt(plx * plx + ply * ply)                      # 粒の中心からの距離 [粒の格子単位]
    p_size = rng.uniform(0.35, 0.7, peb_count)
    p_dome = np.clip(1.0 - (peb_r / p_size[peb_id]) ** 2, 0.0, 1.0)
    p_has = rng.random(peb_count) < 0.8                         # 粒の無い所＝セメント
    pebble = np.sqrt(p_dome) * p_has[peb_id]
    pcolors = np.array(recipe["pebble_colors"])
    p_color = pcolors[rng.integers(0, len(pcolors), peb_count)]
    p_color = p_color * rng.uniform(0.85, 1.15, (peb_count, 1))
    cement_n = tileable_noise(rng, n, 1.2, cutoff=40.0 / n)
    h_grout = -recipe["grout_depth_m"] + pebble * recipe["pebble_height_m"] + (cement_n - 0.5) * 0.002

    height = h_grout + (h_stone - h_grout) * stone_mask

    # ---- 色（陰影は描かない）----
    base = s_gray[stone_id][..., None] * s_tint[stone_id]
    mott = tileable_noise(rng, n, 2.0, cutoff=3.0 / n)
    base = base * (1.0 + (mott[..., None] - 0.5) * 0.18)
    # 花崗岩のごま塩（細かい黒と白の粒）
    sp_d = tileable_noise(rng, n, 0.6, cutoff=180.0 / n)
    sp_l = tileable_noise(rng, n, 0.6, cutoff=180.0 / n)
    dark = np.clip((sp_d - (1.0 - recipe["speckle_dark"] * 0.5)) * 8.0, 0.0, 1.0)
    light = np.clip((sp_l - (1.0 - recipe["speckle_light"] * 0.5)) * 8.0, 0.0, 1.0)
    base = base * (1.0 - dark[..., None] * 0.45) + light[..., None] * 0.12
    # 錆色の染み（斑点状）と筋
    rust_n = tileable_noise(rng, n, 2.0, cutoff=6.0 / n)
    rust = np.clip((rust_n - 0.62) * 4.0, 0.0, 1.0) * s_rust[stone_id]
    ang = s_band_angle[stone_id]
    across = local_x * np.cos(ang) + local_y * np.sin(ang)
    band_w = 0.08 + 0.08 * tileable_noise(rng, n, 2.0, cutoff=4.0 / n)
    band = (1.0 - smoothstep(0.0, band_w, np.abs(across))) ** 1.5 * s_band[stone_id]
    rust = np.clip(rust + band * 0.9, 0.0, 1.0) * recipe["rust_amount"]
    rust = rust * (0.6 + 0.4 * mott)
    rust_col = np.array(recipe["rust_color"]) * (0.7 + 0.6 * s_gray[stone_id][..., None])
    base = base * (1.0 - rust[..., None]) + rust_col * rust[..., None]
    # 縁の欠けた所は少し暗く（汚れが溜まる）
    edge_dirt = (1.0 - smoothstep(0.0, 0.01, d_in)) * 0.18
    stone_col = base * (1.0 - edge_dirt[..., None])

    cement = np.array(recipe["cement_color"]) * (0.85 + cement_n[..., None] * 0.3)
    grout_col = cement * (1.0 - pebble[..., None]) + p_color[peb_id] * pebble[..., None]
    grout_col = grout_col * (0.85 + 0.15 * p_dome[..., None])   # 粒の根元の汚れ

    color = grout_col + (stone_col - grout_col) * stone_mask[..., None]
    return np.clip(color, 0.0, 1.0), height


def height_to_normal(height, px):
    """実寸の勾配から接空間法線（gen_terrain_material.py と同じ規約）"""
    dx = (np.roll(height, -1, axis=1) - np.roll(height, 1, axis=1)) / (2.0 * px)
    dy = (np.roll(height, -1, axis=0) - np.roll(height, 1, axis=0)) / (2.0 * px)
    nx, ny, nz = -dx, -dy, np.ones_like(height)
    length = np.sqrt(nx * nx + ny * ny + nz * nz)
    return np.stack([nx, ny, nz], axis=-1) / length[..., None]


def to_u8(img):
    return Image.fromarray(np.clip(img * 255.0 + 0.5, 0, 255).astype(np.uint8))


def write_preview(out_dir, color, normal):
    """低い角度の光で陰影を付けた確認用画像（実際の描画ではない）。2×2 に並べて継ぎ目も見る"""
    out_dir.mkdir(parents=True, exist_ok=True)
    light = np.array([-0.55, -0.45, 0.70])   # 画像の左上から斜めに当てる（行0=上なので y は負が上）
    light /= np.linalg.norm(light)
    ndl = np.clip((normal * light).sum(-1), 0.0, 1.0)
    lin = color ** 2.2
    shaded = (lin * (0.25 + 1.1 * ndl[..., None])) ** (1.0 / 2.2)
    to_u8(shaded).save(out_dir / "flagstone_shaded.png")
    tiled = np.tile(shaded[::2, ::2], (2, 2, 1))
    to_u8(tiled).save(out_dir / "flagstone_tiled.png")
    n = shaded.shape[0]
    to_u8(shaded[n // 3:n // 3 + 768, n // 3:n // 3 + 768]).save(out_dir / "flagstone_closeup.png")
    print(f"[flagstone] preview -> {out_dir}")


def run_cooker(project_root: Path) -> None:
    cooker = project_root / "tools" / "Python" / "cook_assets.py"
    print("[cook] running cook_assets.py ...")
    result = subprocess.run([sys.executable, str(cooker)], cwd=str(project_root), capture_output=True, text=True)
    for line in result.stdout.splitlines():
        if "Flagstone" in line:
            print(line)
    if result.returncode != 0:
        print(result.stderr.rstrip(), file=sys.stderr)
        raise SystemExit(f"cook_assets.py failed (exit {result.returncode})")


def main() -> None:
    ap = argparse.ArgumentParser(description="乱形石張りの石畳の素材をハイトマップから生成する")
    ap.add_argument("--seed", type=int, default=None)
    ap.add_argument("--name", type=str, default=None)
    ap.add_argument("--preview", type=str, default=None, help="確認用の陰影付き画像の出力先")
    ap.add_argument("--no-cook", action="store_true")
    args = ap.parse_args()

    recipe = dict(RECIPE)
    if args.seed is not None:
        recipe["seed"] = args.seed
    if args.name:
        recipe["name"] = args.name
    name = recipe["name"]
    px = recipe["tile_m"] / recipe["size"]

    print(f"[flagstone] {name}: size={recipe['size']} tile={recipe['tile_m']}m seed={recipe['seed']}")
    color, height = generate(recipe)
    normal = height_to_normal(height, px)

    project_root = Path(__file__).resolve().parents[2]
    assets = project_root / "Assets" / "Textures"
    base_path = assets / "Terrain" / f"{name}_BaseColor.png"
    normal_path = assets / "NormalMapTexture" / "Terrain" / f"{name}_NormalMap.png"
    height_path = assets / "MaskTexture" / "Terrain" / f"{name}_Height.png"
    for p in (base_path, normal_path, height_path):
        p.parent.mkdir(parents=True, exist_ok=True)

    to_u8(color).save(base_path)
    to_u8(normal * 0.5 + 0.5).save(normal_path)
    h_min, h_max = float(height.min()), float(height.max())
    h01 = (height - h_min) / (h_max - h_min)
    to_u8(np.repeat(h01[..., None], 3, axis=-1)).save(height_path)
    height_path.with_suffix(".json").write_text(json.dumps({
        "tile_m": recipe["tile_m"], "height_min_m": h_min, "height_max_m": h_max,
        "note": "Height.png の 0..1 は height_min_m..height_max_m [m]。視差の深さは (max-min)",
    }, indent=2), encoding="utf-8")
    print(f"[flagstone] wrote {base_path.name} / {normal_path.name} / {height_path.name} "
          f"(height {h_min * 1000:.1f}..{h_max * 1000:.1f} mm)")

    if args.preview:
        write_preview(Path(args.preview), color, normal)
    if not args.no_cook:
        run_cooker(project_root)


if __name__ == "__main__":
    main()
