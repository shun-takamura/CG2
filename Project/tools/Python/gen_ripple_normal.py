"""
同心円の波紋の「歪み用ノーマルマップ」を生成する（Distortion エフェクト用）。

  python tools/Python/gen_ripple_normal.py
  python tools/Python/cook_assets.py   # → Resources/Textures/NormalMapTexture/TitleRippleNormal.dds（フォルダ名で線形 BC7）

DistortionMesh.PS の取り決め：RG = 歪み方向（0.5 中心、±0.5）、A = 強度。
波の高さ h(r) = sin(2π·rings·r) を中心と外周で 0 に絞り、その勾配を RG に入れる。
A も同じ絞りにして、板の縁で歪みが途切れないようにする。
タイトルのロゴが消えるときの揺らぎ（Resources/Json/Effects/TitleLogoRipple.json）で使う。
"""
from pathlib import Path

import numpy as np
from PIL import Image

PROJECT_DIR = Path(__file__).resolve().parents[2]
OUT_PATH = PROJECT_DIR / "Assets" / "Textures" / "NormalMapTexture" / "TitleRippleNormal.png"

SIZE = 512
RINGS = 4.0          # 半径方向の波の数
INNER_FADE = 0.12    # 中心から何割まで絞るか
OUTER_FADE = 0.7     # ここから外周（半径 1）に向けて 0 へ
STRENGTH = 0.9       # 勾配のスケール（RG の振れ幅）


def smoothstep(e0, e1, x):
    t = np.clip((x - e0) / (e1 - e0), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def main() -> None:
    c = (np.arange(SIZE) + 0.5) / SIZE * 2.0 - 1.0
    x, y = np.meshgrid(c, c)          # 行 0 = 画像の上
    r = np.sqrt(x * x + y * y)
    envelope = smoothstep(0.0, INNER_FADE, r) * (1.0 - smoothstep(OUTER_FADE, 1.0, r))

    # h = sin(2π·RINGS·r) · envelope の半径方向の微分（envelope の微分は小さいので省く）
    dh_dr = np.cos(2.0 * np.pi * RINGS * r) * envelope
    safe_r = np.maximum(r, 1e-6)
    gx = dh_dr * x / safe_r
    gy = dh_dr * y / safe_r

    rgba = np.zeros((SIZE, SIZE, 4), dtype=np.float32)
    rgba[..., 0] = 0.5 + 0.5 * np.clip(gx * STRENGTH, -1.0, 1.0)
    rgba[..., 1] = 0.5 + 0.5 * np.clip(gy * STRENGTH, -1.0, 1.0)
    rgba[..., 2] = 1.0
    rgba[..., 3] = envelope

    OUT_PATH.parent.mkdir(parents=True, exist_ok=True)
    Image.fromarray((rgba * 255.0 + 0.5).astype(np.uint8), "RGBA").save(OUT_PATH)
    print(f"wrote {OUT_PATH}")


if __name__ == "__main__":
    main()
