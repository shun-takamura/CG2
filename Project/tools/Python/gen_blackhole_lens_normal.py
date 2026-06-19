"""ブラックホールの重力レンズ用ノーマルマップ(歪みマップ)を生成する。

歪みシェーダー(DistortionMesh.PS / Distortion.PS)の規約:
    offset = (RG - 0.5) * 2     … RG が UV ずらし方向
    intensity = A               … A が歪みの強度(0で無歪み)
    distortedUV = uv + offset * intensity * strength

設計:
    - RG = 接線(回り込み) + 放射(レンズ)。中心ほど接線寄り＝近いほど回り込む。
    - A  = 強度。中心の球穴(0) → すぐ外で最大 → 外周へ power 減衰(中心ほど強い)。
    - 渦の腕(spiral arm)を低コントラストで A に焼く。
      → 完全対称だと回しても動いて見えないので、腕があると回転で動きが出る。
    - RG は atan2 ベースの連続場 ＝ 12時などでのプツン切れが無い。

中心が無歪みなので Plane 全面貼りでも中央の球は歪まない(Ring 不要)。

使い方:
    cd Project/
    python tools/Python/gen_blackhole_lens_normal.py
出力:
    Assets/Textures/NormalMapTexture/BlackHoleLensNormal.png
  → cook_assets.py で Resources/.../BlackHoleLensNormal.dds (Linear/BC7) に焼く
"""

from __future__ import annotations

import argparse
from pathlib import Path

import numpy as np
from PIL import Image


def smoothstep(edge0: float, edge1: float, x: np.ndarray) -> np.ndarray:
    t = np.clip((x - edge0) / (edge1 - edge0), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def generate(
    size: int,
    r_inner: float,
    inner_edge: float,
    r_outer: float,
    power: float,
    swirl: float,
    radial: float,
    arms: int,
    arm_contrast: float,
    spiral: float,
    hole: bool,
    r_peak: float,
) -> Image.Image:
    # ピクセル中心を -1..1 に正規化(中心=0、半辺=1)
    coords = (np.arange(size) + 0.5) / size * 2.0 - 1.0
    x = coords[np.newaxis, :].repeat(size, axis=0)   # 右が +x
    y = coords[:, np.newaxis].repeat(size, axis=1)   # 下が +y(UV と一致)

    r = np.sqrt(x * x + y * y)
    r_safe = np.maximum(r, 1e-6)
    theta = np.arctan2(y, x)

    # 単位ベクトル: 放射(外向き) と 接線(反時計まわり)
    radial_x, radial_y = x / r_safe, y / r_safe
    tangent_x, tangent_y = -radial_y, radial_x

    # 減衰の基準半径。明示指定が無ければ従来どおり r_inner+inner_edge。
    r_peak_eff = r_peak if r_peak > 0.0 else (r_inner + inner_edge)

    # 向き: 中心ほど接線寄り(回り込み)、外ほど放射が混ざる。
    radial_mix = radial * np.clip(r / r_peak_eff, 0.0, 1.0)
    off_x = swirl * tangent_x + radial_mix * radial_x
    off_y = swirl * tangent_y + radial_mix * radial_y
    off_len = np.maximum(np.sqrt(off_x * off_x + off_y * off_y), 1e-6)
    off_x, off_y = off_x / off_len, off_y / off_len

    # 強度 A:
    #   hole=True : 中心穴(0) → r_peak で最大 → 外周へ power 減衰
    #   hole=False: 中心から r_peak_eff まで満強度 → 外周へ power 減衰(中心の穴なし)
    rise = smoothstep(r_inner, r_peak_eff, r) if hole else np.ones_like(r)
    decay = np.clip(r_peak_eff / np.maximum(r, r_peak_eff), 0.0, 1.0) ** power
    outer = 1.0 - smoothstep(r_outer * 0.5, r_outer, r)
    strength = rise * decay * outer

    # 渦の腕(対数スパイラル)を低コントラストで重ねる。回すと動きが出る。
    if arms > 0 and arm_contrast > 0.0:
        spin = np.cos(arms * theta + spiral * np.log(r_safe / r_peak_eff))
        arm_mask = (1.0 - arm_contrast) + arm_contrast * (0.5 + 0.5 * spin)
        strength = strength * arm_mask

    strength = np.clip(strength, 0.0, 1.0)

    # 向きの振れ幅も強度で絞る(縁をなめらかに、無歪み域のノイズ感を消す)
    R = 0.5 + 0.5 * off_x * strength
    G = 0.5 + 0.5 * off_y * strength
    B = np.ones_like(r)          # 未使用(法線 z 上向き相当)
    A = strength

    rgba = np.stack([R, G, B, A], axis=-1)
    rgba = np.clip(rgba * 255.0 + 0.5, 0.0, 255.0).astype(np.uint8)
    return Image.fromarray(rgba, mode="RGBA")


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--size", type=int, default=512)
    ap.add_argument("--r-inner", type=float, default=0.24, help="A=0 の中心穴半径(球を歪ませない範囲)")
    ap.add_argument("--inner-edge", type=float, default=0.05, help="穴の外側で強度が立ち上がる幅(ピーク=r_inner+inner_edge)")
    ap.add_argument("--r-outer", type=float, default=1.05, help="A=0 へ戻る外周半径")
    ap.add_argument("--power", type=float, default=1.8, help="外周への減衰の急さ(大きいほど中心集中)")
    ap.add_argument("--swirl", type=float, default=1.0, help="接線(回り込み)成分")
    ap.add_argument("--radial", type=float, default=-0.35, help="放射成分(負=内向きに引き込む。中心ほど弱める)")
    ap.add_argument("--arms", type=int, default=2, help="渦の腕の本数(0で対称・腕なし)")
    ap.add_argument("--arm-contrast", type=float, default=0.4, help="腕の濃淡(0=対称, 1=最大)")
    ap.add_argument("--spiral", type=float, default=5.0, help="腕のねじれ(対数スパイラルの巻き)")
    ap.add_argument("--no-hole", dest="hole", action="store_false", help="中心の無歪み穴をなくし中心まで歪ませる")
    ap.add_argument("--r-peak", type=float, default=0.0, help="減衰の基準(満強度)半径。0で自動。--no-hole 時はこの内側が満強度")
    ap.set_defaults(hole=True)
    ap.add_argument(
        "--out",
        type=str,
        default="Assets/Textures/NormalMapTexture/BlackHoleLensNormal.png",
    )
    args = ap.parse_args()

    img = generate(
        args.size, args.r_inner, args.inner_edge, args.r_outer, args.power,
        args.swirl, args.radial, args.arms, args.arm_contrast, args.spiral,
        args.hole, args.r_peak,
    )
    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    img.save(out)
    print(f"wrote {out} ({args.size}x{args.size} RGBA)")


if __name__ == "__main__":
    main()
