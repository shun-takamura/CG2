"""地形/岩などのタイリング素材(ベースカラー + 法線マップ)を手続き的に生成する。

設計:
    - FBM(値ノイズの多重重ね)でタイリング可能なハイトフィールドを作る。
      タイリング性は「格子ノイズの格子をトーラス状に wrap する」ことで担保する。
    - 法線マップはハイトフィールドの中心差分勾配から算出。
      N = normalize(-dH/dx * bump, -dH/dy * bump, 1) を RGB へ 0.5+0.5*N でエンコード。
      → Object3dPBR.PS.hlsl 側は `gNormalMap.Sample(...).xyz * 2 - 1` でデコードするので整合する。
      メッシュ自体は平らなまま、法線だけで凹凸があるかのような陰影が出る。
    - ベースカラーは「高さ」と「傾斜(slope)」で low/mid/high の3色をブレンド。
      陰影・ハイライトは焼き込まない(動的ライティングと二重がけになるため)。

このスクリプトはテクスチャ(PNG)を出すだけで、.mat 割り当てやメッシュには一切触れない。
どのメッシュ/サブメッシュがどのテクスチャを使うかは Blender 側のマテリアルノードで決め、
glTF エクスポート時に baseColorTexture / normalTexture として焼き込まれる。
→ 将来この生成部を AI 画像生成に差し替えても、クック/配置ステージは変更不要。

使い方:
    cd Project/
    python tools/Python/gen_terrain_material.py --name RockyDirt01
    python tools/Python/gen_terrain_material.py --recipe tools/Python/recipes/rocky_dirt.json
    python tools/Python/gen_terrain_material.py --name Test --no-cook   # クックをスキップ

出力:
    Assets/Textures/Terrain/<name>_BaseColor.png
    Assets/Textures/NormalMapTexture/Terrain/<name>_NormalMap.png
  → 最後に cook_assets.py を自動実行して Resources/.../<name>_*.dds へ焼く。
     法線マップはパスに "NormalMap" を含むため Linear/BC7(非sRGB)で圧縮される。
"""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
from pathlib import Path

import numpy as np
from PIL import Image


# 既定レシピ。--recipe の JSON はこの辞書へ上書きマージされる。
DEFAULT_RECIPE: dict = {
    "name": "TerrainMaterial",
    "seed": 1234,
    "size": 1024,
    "scale": 6.0,           # ベース格子の分割数(大きいほど細かい模様)
    "octaves": 5,           # FBM の重ね回数
    "persistence": 0.5,     # オクターブごとの振幅減衰
    "lacunarity": 2.0,      # オクターブごとの周波数倍率
    "bump_strength": 1.4,   # 凹凸の見かけの強さ(大きいほど法線が寝る=陰影が強い)
    "colors": {
        "low":  [0.30, 0.24, 0.18],   # 窪み(目地・割れ目)
        "mid":  [0.42, 0.38, 0.28],   # 標準面
        "high": [0.55, 0.53, 0.48],   # 出っ張りの頂点
    },
    "slope_rock_color": [0.38, 0.36, 0.34],  # 急斜面に乗る岩色
    "slope_rock_threshold": 0.55,            # この傾斜を超えると岩色へ寄る
    "color_jitter": 0.04,                    # 画素ごとの微細な色ムラ(のっぺり防止)
}


def _smoothstep(t: np.ndarray) -> np.ndarray:
    return t * t * (3.0 - 2.0 * t)


def _value_noise_tileable(size: int, cells: int, rng: np.random.Generator) -> np.ndarray:
    """周期 cells の格子で値ノイズを作る。格子を wrap するのでタイリングする。"""
    # 格子点の乱数値。端は反対側と同じ値になるよう wrap して参照する。
    lattice = rng.random((cells, cells), dtype=np.float64)

    # 各ピクセルが属する格子座標(0..cells)
    coords = (np.arange(size) + 0.5) / size * cells
    xi = np.floor(coords).astype(np.int64)
    tx = coords - xi
    xi0 = xi % cells
    xi1 = (xi + 1) % cells   # ← wrap するのでタイリング境界で不連続にならない

    # 2D 用に broadcast(行=y, 列=x)
    y0 = xi0[:, np.newaxis]
    y1 = xi1[:, np.newaxis]
    ty = _smoothstep(tx)[:, np.newaxis]
    x0 = xi0[np.newaxis, :]
    x1 = xi1[np.newaxis, :]
    sx = _smoothstep(tx)[np.newaxis, :]

    v00 = lattice[y0, x0]
    v01 = lattice[y0, x1]
    v10 = lattice[y1, x0]
    v11 = lattice[y1, x1]

    top = v00 + (v01 - v00) * sx
    bottom = v10 + (v11 - v10) * sx
    return top + (bottom - top) * ty


def generate_heightfield(recipe: dict) -> np.ndarray:
    """FBM でタイリング可能なハイトフィールド(0..1 正規化)を作る。"""
    size = int(recipe["size"])
    rng = np.random.default_rng(int(recipe["seed"]))

    height = np.zeros((size, size), dtype=np.float64)
    amplitude = 1.0
    frequency = float(recipe["scale"])
    total_amplitude = 0.0

    for _ in range(int(recipe["octaves"])):
        cells = max(2, int(round(frequency)))
        # cells が size を超えるとエイリアスするだけなので打ち切る
        if cells > size:
            break
        height += amplitude * _value_noise_tileable(size, cells, rng)
        total_amplitude += amplitude
        amplitude *= float(recipe["persistence"])
        frequency *= float(recipe["lacunarity"])

    if total_amplitude > 0.0:
        height /= total_amplitude

    # 0..1 へ正規化(レシピによってはコントラストが浅くなるため)
    h_min, h_max = float(height.min()), float(height.max())
    if h_max - h_min > 1e-9:
        height = (height - h_min) / (h_max - h_min)
    return height


def heightfield_to_normal_map(height: np.ndarray, bump_strength: float) -> tuple[Image.Image, np.ndarray]:
    """ハイトフィールドの勾配から接空間法線マップを作る。slope も返す(ベースカラーで使う)。

    np.roll による中心差分なので端も wrap する = タイリング境界でも法線が連続する。
    """
    # dH/dx, dH/dy。roll(-1) が「次のピクセル」、roll(+1) が「前のピクセル」。
    dx = (np.roll(height, -1, axis=1) - np.roll(height, 1, axis=1)) * 0.5
    dy = (np.roll(height, -1, axis=0) - np.roll(height, 1, axis=0)) * 0.5

    # 高さの傾きを法線に変換。bump_strength を上げるほど法線が寝る = 陰影が強い。
    nx = -dx * bump_strength * height.shape[1]
    ny = -dy * bump_strength * height.shape[0]
    nz = np.ones_like(height)

    length = np.sqrt(nx * nx + ny * ny + nz * nz)
    nx, ny, nz = nx / length, ny / length, nz / length

    # slope: 法線がどれだけ寝ているか(0=平ら, 1=垂直)
    slope = np.clip(1.0 - nz, 0.0, 1.0)

    # シェーダーの `xyz * 2 - 1` に合わせて 0.5 + 0.5 * N でエンコード
    rgb = np.stack([nx, ny, nz], axis=-1) * 0.5 + 0.5
    rgb_u8 = np.clip(rgb * 255.0 + 0.5, 0.0, 255.0).astype(np.uint8)
    return Image.fromarray(rgb_u8, mode="RGB"), slope


def heightfield_to_base_color(height: np.ndarray, slope: np.ndarray, recipe: dict) -> Image.Image:
    """高さと傾斜で色をブレンドしたアルベドを作る。影・ハイライトは焼き込まない。"""
    colors = recipe["colors"]
    low = np.array(colors["low"], dtype=np.float64)
    mid = np.array(colors["mid"], dtype=np.float64)
    high = np.array(colors["high"], dtype=np.float64)

    h = height[..., np.newaxis]

    # low → mid → high の2段ブレンド
    t_lo = np.clip(h * 2.0, 0.0, 1.0)
    t_hi = np.clip((h - 0.5) * 2.0, 0.0, 1.0)
    rgb = low + (mid - low) * _smoothstep(t_lo)
    rgb = rgb + (high - rgb) * _smoothstep(t_hi)

    # 急斜面(割れ目の壁面など)は岩色へ寄せる
    rock = np.array(recipe["slope_rock_color"], dtype=np.float64)
    threshold = float(recipe["slope_rock_threshold"])
    slope_norm = slope / max(slope.max(), 1e-9)
    t_rock = _smoothstep(np.clip((slope_norm - threshold) / max(1.0 - threshold, 1e-9), 0.0, 1.0))
    rgb = rgb + (rock - rgb) * t_rock[..., np.newaxis]

    # 画素ごとの微細な色ムラ(のっぺり防止)。seed をずらして高さと相関させない。
    jitter = float(recipe.get("color_jitter", 0.0))
    if jitter > 0.0:
        rng = np.random.default_rng(int(recipe["seed"]) + 977)
        noise = (rng.random(height.shape, dtype=np.float64) - 0.5) * 2.0 * jitter
        rgb = rgb + noise[..., np.newaxis]

    rgb_u8 = np.clip(rgb * 255.0 + 0.5, 0.0, 255.0).astype(np.uint8)
    return Image.fromarray(rgb_u8, mode="RGB")


def load_recipe(args: argparse.Namespace) -> dict:
    """既定レシピに --recipe JSON と個別 CLI 引数を順に上書きする。"""
    recipe = json.loads(json.dumps(DEFAULT_RECIPE))  # deep copy

    if args.recipe:
        path = Path(args.recipe)
        if not path.exists():
            raise SystemExit(f"recipe not found: {path}")
        with path.open("r", encoding="utf-8") as f:
            user = json.load(f)
        for key, value in user.items():
            # colors だけは部分指定を許す(low だけ変えたい等)
            if key == "colors" and isinstance(value, dict):
                recipe["colors"].update(value)
            else:
                recipe[key] = value

    # 明示指定された CLI 引数だけを上書き(未指定は None にしてある)
    for key in ("name", "seed", "size", "scale", "octaves", "bump_strength"):
        value = getattr(args, key, None)
        if value is not None:
            recipe[key] = value

    return recipe


def run_cooker(project_root: Path) -> None:
    """cook_assets.py を呼んで PNG を DDS へ焼く。常駐ウォッチャーは作らない。"""
    cooker = project_root / "tools" / "Python" / "cook_assets.py"
    if not cooker.exists():
        print(f"[warn] cooker not found, skipped: {cooker}")
        return

    print(f"[cook] running {cooker.name} ...")
    result = subprocess.run(
        [sys.executable, str(cooker)],
        cwd=str(project_root),      # cook_assets.py は CWD=Project/ 前提
        capture_output=True,
        text=True,
    )
    if result.stdout:
        print(result.stdout.rstrip())
    if result.returncode != 0:
        print(result.stderr.rstrip(), file=sys.stderr)
        raise SystemExit(f"cook_assets.py failed (exit {result.returncode})")


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--recipe", type=str, default=None, help="レシピ JSON のパス(既定値へ上書きマージ)")
    # 以下は未指定なら None = レシピ側の値を使う
    ap.add_argument("--name", type=str, default=None, help="素材名(出力ファイル名の接頭辞)")
    ap.add_argument("--seed", type=int, default=None, help="乱数シード(同じ値なら同じ模様)")
    ap.add_argument("--size", type=int, default=None, help="出力解像度(正方)")
    ap.add_argument("--scale", type=float, default=None, help="模様の細かさ")
    ap.add_argument("--octaves", type=int, default=None, help="FBM のオクターブ数")
    ap.add_argument("--bump-strength", dest="bump_strength", type=float, default=None,
                    help="凹凸の強さ(大きいほど陰影が濃い)")
    ap.add_argument("--project-root", type=str, default=".",
                    help="Project/ ディレクトリ(既定=カレント)")
    ap.add_argument("--no-cook", dest="cook", action="store_false",
                    help="PNG 生成のみ行い cook_assets.py を実行しない")
    ap.set_defaults(cook=True)
    args = ap.parse_args()

    project_root = Path(args.project_root).resolve()
    if not (project_root / "Assets").exists():
        raise SystemExit(f"Assets/ が見つかりません。Project/ で実行するか --project-root を指定してください: {project_root}")

    recipe = load_recipe(args)
    name = str(recipe["name"])

    print(f"[gen] {name}: size={recipe['size']} seed={recipe['seed']} "
          f"scale={recipe['scale']} octaves={recipe['octaves']} bump={recipe['bump_strength']}")

    height = generate_heightfield(recipe)
    normal_img, slope = heightfield_to_normal_map(height, float(recipe["bump_strength"]))
    base_color_img = heightfield_to_base_color(height, slope, recipe)

    # 法線マップはパス要素に "NormalMap" を含めること(cook_assets.py の LINEAR_TEXTURE_HINTS)。
    # ここを外すと sRGB として BC7 圧縮され、法線が壊れる。
    base_color_out = project_root / "Assets" / "Textures" / "Terrain" / f"{name}_BaseColor.png"
    normal_out = project_root / "Assets" / "Textures" / "NormalMapTexture" / "Terrain" / f"{name}_NormalMap.png"

    base_color_out.parent.mkdir(parents=True, exist_ok=True)
    normal_out.parent.mkdir(parents=True, exist_ok=True)
    base_color_img.save(base_color_out)
    normal_img.save(normal_out)

    print(f"[gen] wrote {base_color_out.relative_to(project_root)}")
    print(f"[gen] wrote {normal_out.relative_to(project_root)}")

    if args.cook:
        run_cooker(project_root)

    print("[done] Blender 側でこの2枚をマテリアルの Base Color / Normal に割り当ててエクスポートしてください。")


if __name__ == "__main__":
    main()
