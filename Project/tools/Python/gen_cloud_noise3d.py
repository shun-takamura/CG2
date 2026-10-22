"""雲海のボリューム用の 3D ノイズを焼く（Nubis / Horizon Zero Dawn の雲と同じ構成）。

拡張子は .vol（中身は DDS のボリューム）。.dds にすると pack_assets.py が 2D 用の配置変換と GDeflate 圧縮をかけて
CPU で読めなくなるので、ASSET_OTHER としてそのまま pack に入れる。

  Resources/Textures/Cloud/cloud_shape3d.vol   128^3 RGBA8・ミップ付き・周期境界
      R = Perlin-Worley（Perlin の fBm を Worley の fBm で削った塊）
      G / B / A = 反転 Worley の fBm（周期 4 / 8 / 16 セル）。R の形をさらに削る低周波の塊
  Resources/Textures/Cloud/cloud_detail3d.vol  32^3 RGBA8・ミップ付き・周期境界
      R / G / B = 反転 Worley の fBm（周期 2 / 4 / 8 セル）。縁を細かく削って「カリフラワー」にする

    py tools\\Python\\gen_cloud_noise3d.py
"""
import argparse
from pathlib import Path

import numpy as np

SHAPE_OUTPUT = Path("Resources/Textures/Cloud/cloud_shape3d.vol")
DETAIL_OUTPUT = Path("Resources/Textures/Cloud/cloud_detail3d.vol")
DXGI_FORMAT_R8G8B8A8_UNORM = 28


def grid(size, period):
    c = (np.arange(size, dtype=np.float32) + 0.5) / size * period
    return np.meshgrid(c, c, c, indexing="ij")  # [z, y, x] の順で使う（X=最後の軸）


# なめらかな最小の鋭さ（セル単位の距離に掛ける）。ただの min だとセルの境目が折れ目になり、
# 浅い角度で見下ろす雲海では折れ目が等高線のような弧の縞に見えた（2026-10-23）
SMOOTH_K = 9.0


def worley(size, period, rng):
    """最近傍の特徴点までの距離（セル単位、0..約1）。周期境界。境目はなめらかな最小で丸める"""
    Z, Y, X = grid(size, period)
    ci = np.floor(X).astype(np.int32)
    cj = np.floor(Y).astype(np.int32)
    ck = np.floor(Z).astype(np.int32)
    pts = rng.random((period, period, period, 3), dtype=np.float32)
    acc = np.zeros(X.shape, dtype=np.float64)
    for oz in (-1, 0, 1):
        for oy in (-1, 0, 1):
            for ox in (-1, 0, 1):
                cx, cy, cz = ci + ox, cj + oy, ck + oz
                p = pts[cz % period, cy % period, cx % period]
                dx = X - (cx + p[..., 0])
                dy = Y - (cy + p[..., 1])
                dz = Z - (cz + p[..., 2])
                acc += np.exp(-SMOOTH_K * np.sqrt(dx * dx + dy * dy + dz * dz))
    return (-np.log(acc) / SMOOTH_K).astype(np.float32)


def perlin(size, period, rng):
    Z, Y, X = grid(size, period)
    xi = np.floor(X).astype(np.int32)
    yi = np.floor(Y).astype(np.int32)
    zi = np.floor(Z).astype(np.int32)
    xf, yf, zf = X - xi, Y - yi, Z - zi
    g = rng.normal(size=(period, period, period, 3)).astype(np.float32)
    g /= np.linalg.norm(g, axis=-1, keepdims=True)

    def fade(t):
        return t * t * t * (t * (t * 6 - 15) + 10)

    u, v, w = fade(xf), fade(yf), fade(zf)
    total = np.zeros(X.shape, dtype=np.float32)
    for oz in (0, 1):
        for oy in (0, 1):
            for ox in (0, 1):
                gg = g[(zi + oz) % period, (yi + oy) % period, (xi + ox) % period]
                d = gg[..., 0] * (xf - ox) + gg[..., 1] * (yf - oy) + gg[..., 2] * (zf - oz)
                wgt = (u if ox else 1 - u) * (v if oy else 1 - v) * (w if oz else 1 - w)
                total += wgt * d
    return total  # おおよそ ±0.7


def worley_fbm(size, period, rng):
    """反転 Worley の 3 オクターブ（0..1、1 = 塊の中心）"""
    a = 1.0 - np.clip(worley(size, period, rng), 0, 1)
    b = 1.0 - np.clip(worley(size, period * 2, rng), 0, 1)
    c = 1.0 - np.clip(worley(size, period * 4, rng), 0, 1)
    return a * 0.625 + b * 0.25 + c * 0.125


def remap(v, lo, hi, nlo, nhi):
    return nlo + (v - lo) / (hi - lo) * (nhi - nlo)


def normalize01(v):
    lo, hi = np.percentile(v, 0.5), np.percentile(v, 99.5)
    return np.clip((v - lo) / max(hi - lo, 1e-6), 0, 1)


def build_shape(size, seed):
    rng = np.random.default_rng(seed)
    # Perlin の fBm（周期 4 から 4 オクターブ）を 0..1 に
    pfbm = np.zeros((size,) * 3, dtype=np.float32)
    amp = 1.0
    for i in range(4):
        pfbm += amp * perlin(size, 4 * 2 ** i, rng)
        amp *= 0.5
    pfbm = normalize01(pfbm)
    w = worley_fbm(size, 4, rng)
    # Perlin-Worley：Perlin の値を Worley の塊で持ち上げる（Nubis：remap(perlin, 0, 1, worley, 1)）
    pw = np.clip(remap(pfbm, 0.0, 1.0, w, 1.0), 0, 1)
    pw = normalize01(pw)
    g = normalize01(worley_fbm(size, 4, rng))
    b = normalize01(worley_fbm(size, 8, rng))
    a = normalize01(worley_fbm(size, 16, rng))
    return [pw, g, b, a]


def build_detail(size, seed):
    rng = np.random.default_rng(seed + 77)
    r = normalize01(worley_fbm(size, 2, rng))
    g = normalize01(worley_fbm(size, 4, rng))
    b = normalize01(worley_fbm(size, 8, rng))
    return [r, g, b, np.ones_like(r)]


def write_dds_volume_rgba8(path, channels):
    """RGBA8 の 3D テクスチャ（DX10 ヘッダ・ミップ付き）"""
    vol = np.stack(channels, axis=-1).astype(np.float32)  # [z, y, x, 4]
    levels = []
    while True:
        levels.append(np.round(np.clip(vol, 0, 1) * 255).astype(np.uint8))
        if vol.shape[0] == 1:
            break
        vol = 0.125 * (vol[0::2, 0::2, 0::2] + vol[1::2, 0::2, 0::2] + vol[0::2, 1::2, 0::2] + vol[0::2, 0::2, 1::2]
                       + vol[1::2, 1::2, 0::2] + vol[1::2, 0::2, 1::2] + vol[0::2, 1::2, 1::2] + vol[1::2, 1::2, 1::2])
    size = channels[0].shape[0]
    header = np.zeros(31, dtype=np.uint32)
    header[0] = 124
    header[1] = 0x1 | 0x2 | 0x4 | 0x1000 | 0x20000 | 0x8 | 0x800000  # ... | DEPTH
    header[2] = size
    header[3] = size
    header[4] = size * 4
    header[5] = size                        # depth
    header[6] = len(levels)
    header[18] = 32
    header[19] = 0x4
    header[20] = int.from_bytes(b"DX10", "little")
    header[26] = 0x1000 | 0x8 | 0x400000    # TEXTURE | COMPLEX | MIPMAP
    header[27] = 0x200000                   # DDSCAPS2_VOLUME
    dx10 = np.array([DXGI_FORMAT_R8G8B8A8_UNORM, 4, 0, 1, 0], dtype=np.uint32)  # TEXTURE3D
    path.parent.mkdir(parents=True, exist_ok=True)
    with open(path, "wb") as f:
        f.write(b"DDS ")
        f.write(header.tobytes())
        f.write(dx10.tobytes())
        for level in levels:
            f.write(level.tobytes())
    print(f"wrote {path} ({size}^3 RGBA8, mips {len(levels)})")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--shape-size", type=int, default=128)
    ap.add_argument("--detail-size", type=int, default=32)
    ap.add_argument("--seed", type=int, default=3)
    ap.add_argument("--preview", type=Path, help="各チャンネルの断面を PNG で保存")
    a = ap.parse_args()
    shape = build_shape(a.shape_size, a.seed)
    detail = build_detail(a.detail_size, a.seed)
    write_dds_volume_rgba8(SHAPE_OUTPUT, shape)
    write_dds_volume_rgba8(DETAIL_OUTPUT, detail)
    if a.preview:
        from PIL import Image
        rows = []
        for ch in shape:
            rows.append(np.concatenate([ch[0], ch[a.shape_size // 3]], axis=1))
        img = np.concatenate(rows, axis=0)
        Image.fromarray(np.round(img * 255).astype(np.uint8), "L").save(a.preview)


if __name__ == "__main__":
    main()
