"""雲用のタイル可能な 2D ノイズを焼いて Resources/Textures/Cloud/cloud_noise.dds に出力する。

    R = 形（Perlin fBm と 反転 Worley fBm の合成。丸い塊がうねりに乗る）
    G = マスク（低周波の Perlin fBm。雲のある所/無い所の大きな分布）

どちらもヒストグラム平坦化して 0..1 に一様に分布させてある。
→ シェーダのしきい値（補正値）t を超える面積が、ほぼ (1 - t) になる＝被覆率として読める。

周期は全オクターブで整数なので、上下左右がそのまま繋がる（WRAP サンプラでタイルする）。
近景のレイマーチ（9_CloudRendering.md フェーズ3）でも同じテクスチャを使う。

出力は BC5_UNORM（2ch・線形）＋ミップ。中間の PNG は一時フォルダに置く。
Assets/ に PNG を置くと cook_assets.py が BC7_SRGB で上書きするので置かないこと。

    py tools\\Python\\gen_cloud_noise.py
    py tools\\Python\\gen_cloud_noise.py --seed 7 --size 1024
    py tools\\Python\\gen_cloud_noise.py --preview cloud_noise_preview.png   # 確認用 PNG も残す
"""
import argparse
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np
from PIL import Image

TEXCONV = Path("externals/texconv/Texconv.exe")
OUTPUT = Path("Resources/Textures/Cloud/cloud_noise.dds")


def fade(t):
    return t * t * t * (t * (t * 6.0 - 15.0) + 10.0)


def grid(size, period):
    """ピクセル中心を [0, period) の格子座標へ。戻り値 X[y, x], Y[y, x]"""
    c = (np.arange(size) + 0.5) / size * period
    return np.meshgrid(c, c, indexing="xy")


def perlin(size, period, rng):
    X, Y = grid(size, period)
    xi = np.floor(X).astype(np.int64)
    yi = np.floor(Y).astype(np.int64)
    xf = X - xi
    yf = Y - yi

    angle = rng.uniform(0.0, 2.0 * np.pi, (period, period))
    gx = np.cos(angle)
    gy = np.sin(angle)

    def corner(ix, iy, dx, dy):
        ix = ix % period
        iy = iy % period
        return gx[iy, ix] * dx + gy[iy, ix] * dy

    n00 = corner(xi, yi, xf, yf)
    n10 = corner(xi + 1, yi, xf - 1.0, yf)
    n01 = corner(xi, yi + 1, xf, yf - 1.0)
    n11 = corner(xi + 1, yi + 1, xf - 1.0, yf - 1.0)
    u = fade(xf)
    v = fade(yf)
    return (n00 + (n10 - n00) * u) * (1.0 - v) + (n01 + (n11 - n01) * u) * v


def worley(size, period, rng):
    """最近傍の特徴点までの距離（セル単位）。周期境界で折り返す"""
    X, Y = grid(size, period)
    ci = np.floor(X).astype(np.int64)
    cj = np.floor(Y).astype(np.int64)
    points = rng.random((period, period, 2))

    nearest = np.full(X.shape, np.inf)
    for oy in (-1, 0, 1):
        for ox in (-1, 0, 1):
            cx = ci + ox
            cy = cj + oy
            p = points[cy % period, cx % period]
            d = np.hypot(X - (cx + p[..., 0]), Y - (cy + p[..., 1]))
            nearest = np.minimum(nearest, d)
    return nearest


def fbm(fn, size, base_period, octaves, rng):
    total = np.zeros((size, size))
    amplitude = 1.0
    for i in range(octaves):
        total += amplitude * fn(size, base_period * (2 ** i), rng)
        amplitude *= 0.5
    return total


def equalize(values):
    """値の順位で 0..1 に一様化（ヒストグラム平坦化）"""
    flat = values.ravel()
    ranks = np.empty(flat.size)
    ranks[np.argsort(flat, kind="stable")] = np.arange(flat.size)
    return (ranks / (flat.size - 1)).reshape(values.shape)


def build(size, seed):
    rng = np.random.default_rng(seed)

    # 形：うねり（Perlin）に丸い塊（反転 Worley）を乗せる
    billow = equalize(fbm(perlin, size, 4, 5, rng))
    blobs = equalize(fbm(lambda s, p, r: 1.0 - np.clip(worley(s, p, r), 0.0, 1.0), size, 6, 3, rng))
    shape = equalize(0.55 * billow + 0.45 * blobs)

    # マスク：大きな分布だけ欲しいので低周波・少オクターブ
    mask = equalize(fbm(perlin, size, 2, 3, rng))
    return shape, mask


def to_png(shape, mask, path):
    rgb = np.zeros(shape.shape + (3,), dtype=np.uint8)
    rgb[..., 0] = np.round(shape * 255.0).astype(np.uint8)
    rgb[..., 1] = np.round(mask * 255.0).astype(np.uint8)
    Image.fromarray(rgb, "RGB").save(path)


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--size", type=int, default=512)
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--preview", type=Path, help="中間 PNG をこのパスにも保存する")
    args = parser.parse_args(argv)

    if not TEXCONV.exists():
        print(f"[ERROR] {TEXCONV} が見つかりません（Project/ をカレントにして実行してください）")
        return 1

    shape, mask = build(args.size, args.seed)
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)

    with tempfile.TemporaryDirectory() as tmp:
        png = Path(tmp) / (OUTPUT.stem + ".png")
        to_png(shape, mask, png)
        if args.preview:
            shutil.copyfile(png, args.preview)
            print(f"preview: {args.preview}")

        cmd = [
            str(TEXCONV), "-nologo", "-y",
            "-f", "BC5_UNORM",
            "-dx10",          # pack_assets は DXT10 ヘッダ前提
            "-m", "0",        # ミップを最後まで作る（水平線のちらつき対策）
            "-wrap",          # ミップ生成時に端を折り返す（タイルの継ぎ目を出さない）
            "-o", str(OUTPUT.parent),
            str(png),
        ]
        result = subprocess.run(cmd, capture_output=True, text=True)
        if result.returncode != 0:
            print(f"[ERROR] texconv failed (exit {result.returncode})")
            print(result.stdout)
            print(result.stderr)
            return 1

    print(f"wrote {OUTPUT} ({args.size}x{args.size}, seed={args.seed})")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
