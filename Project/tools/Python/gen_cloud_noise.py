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

--moko を付けると、上の代わりに「もこもこ」の高さを焼いて Resources/Textures/Cloud/cloud_moko_height.dds に出力する（BC4・1ch）。

大中小の半球（ドーム）をセルに1つずつ置き、なめらかな max（log-sum-exp）で重ねる。ただの max だと谷が鋭く折れて鱗に見える。
座標はドメインワープでゆがませ、半径も大きくばらつかせる（同じ円が並ぶと泡や鱗に見えて人工的になる）。
雲の位置とは無関係なタイル可能な細部で、シェーダでは次の3つに使い回す:
    密度   … 形のノイズに足して輪郭をこぶ状にちぎる（もこもこ感の大半は輪郭から来る）
    小さい影 … 太陽側へ数テクセルずらした高さと比べ、こぶの太陽側を明るく・谷を灰色にする
    リム   … 薄い所（輪郭）を明るくする
法線では持たない。法線で照らすと谷が線になってエンボス調に見えるため。
高さの比較は数テクセル離れた位置どうしなので、8bit の段差が筋にならない。

    py tools\Python\gen_cloud_noise.py --moko --no-dds --preview moko.png   # 見た目だけ確認
    py tools\Python\gen_cloud_noise.py --moko --recipe large --tile 2 --no-dds --preview moko_large.png
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
MOKO_OUTPUT = Path("Resources/Textures/Cloud/cloud_moko_height.dds")

# もこもこのレシピ。octaves = (セルの周期, 重み)。周期が大きいほど粒が小さい
#   radius … ドームの半径（セル単位）。1 を超えると隣と大きく重なって粒が潰れる
#   jitter … 半径のばらつき（0 で全部同じ大きさ＝人工的）
#   smooth … なめらかな max の鋭さ。大きいほど max に近づき谷が折れる
#   warp   … ドメインワープの強さ（セル単位）。円がゆがむ。強すぎると筋状に流れる
#   lobe   … こぶごとに半径を角度で変える量（2〜4倍の波をランダムな位相で足す）。0 で真円
#   billow … |Perlin| の fBm（決まった形を持たないうねり）を混ぜる割合。0 でドームだけ
MOKO_RECIPES = {
    "default": dict(octaves=((6, 1.0), (12, 0.5), (24, 0.25), (48, 0.125), (96, 0.0625)),
                    radius=0.85, jitter=0.7, smooth=6.0, warp=0.25, lobe=0.0, billow=0.0),
    # 細かい段を弱めて大きなこぶ主体に（参考写真の積雲寄り）
    "large":   dict(octaves=((4, 1.0), (8, 0.6), (16, 0.3), (32, 0.12)),
                    radius=0.9, jitter=0.6, smooth=5.0, warp=0.3, lobe=0.0, billow=0.0),
    # 大きなこぶの縁に小さなこぶを強めに残す
    "cauli":   dict(octaves=((4, 1.0), (10, 0.7), (24, 0.4), (56, 0.15)),
                    radius=0.85, jitter=0.7, smooth=6.0, warp=0.25, lobe=0.0, billow=0.0),
    # large のこぶをいびつにする
    "large_lobe":   dict(octaves=((4, 1.0), (8, 0.6), (16, 0.3), (32, 0.12)),
                         radius=0.9, jitter=0.6, smooth=5.0, warp=0.4, lobe=0.3, billow=0.0),
    # large に形を持たないうねりを混ぜる
    "large_billow": dict(octaves=((4, 1.0), (8, 0.6), (16, 0.3), (32, 0.12)),
                         radius=0.9, jitter=0.6, smooth=5.0, warp=0.3, lobe=0.0, billow=0.45),
    # 両方（強め）
    "large_irregular": dict(octaves=((4, 1.0), (8, 0.6), (16, 0.3), (32, 0.12)),
                            radius=0.9, jitter=0.6, smooth=5.0, warp=0.4, lobe=0.3, billow=0.35),
    # 両方（控えめ）。こぶの丸みを少し残す
    "large_soft": dict(octaves=((4, 1.0), (8, 0.6), (16, 0.3), (32, 0.12)),
                       radius=0.9, jitter=0.6, smooth=4.0, warp=0.35, lobe=0.2, billow=0.25),
}

BILLOW_SOFTNESS = 0.12    # billow の折れ目の丸め幅（Perlin の値はおおよそ ±0.7）

# プレビューの見え方。シェーダで使う予定の式と同じ値の意味で持つ
PREVIEW = dict(
    coverage=0.6, sharpness=4.5,      # 今の CloudLayer の既定に近い値
    moko_amount=0.3,                  # 密度に足す量
    sun_step=(5, -4), sun_steps=3,    # 大きい影：太陽側へ何テクセルずつ何歩
    density=0.35,                     # 大きい影の透過率の強さ
    micro_step=(2, 2), micro_gain=6.0,# 小さい影：比べる距離と強さ
    lit=(1.0, 1.0, 1.0), shadow=(0.42, 0.46, 0.55), rim=(0.22, 0.22, 0.2),
    sky=(0.10, 0.25, 0.75),
)


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


def domes(size, period, recipe, rng, warp_x, warp_y):
    """セルごとに1つ置いた半球の高さ（0..1）を、なめらかな max で重ねたもの。周期境界で折り返す"""
    X, Y = grid(size, period)
    X = X + warp_x * recipe["warp"]
    Y = Y + warp_y * recipe["warp"]
    ci = np.floor(X).astype(np.int64)
    cj = np.floor(Y).astype(np.int64)
    points = rng.random((period, period, 2))
    jitter = recipe["jitter"]
    radius = recipe["radius"] * (1.0 - jitter + jitter * rng.random((period, period)))
    smooth = recipe["smooth"]
    # こぶごとの角度の波（2,3,4 倍）。振幅は合計が lobe になるよう 1/2, 1/3, 1/6 に振る
    phases = rng.uniform(0.0, 2.0 * np.pi, (period, period, 3))
    lobe = recipe["lobe"]

    acc = np.zeros(X.shape)
    for oy in (-1, 0, 1):
        for ox in (-1, 0, 1):
            cx = (ci + ox) % period
            cy = (cj + oy) % period
            p = points[cy, cx]
            vx = X - (ci + ox + p[..., 0])
            vy = Y - (cj + oy + p[..., 1])
            r = radius[cy, cx]
            if lobe > 0.0:
                theta = np.arctan2(vy, vx)
                ph = phases[cy, cx]
                wave = (np.cos(2.0 * theta + ph[..., 0]) / 2.0
                        + np.cos(3.0 * theta + ph[..., 1]) / 3.0
                        + np.cos(4.0 * theta + ph[..., 2]) / 6.0)
                # 中心付近まで角度の差を効かせると放射状の筋になるので、縁（半径の 4 割より外）だけで効かせる
                edge = np.clip((np.hypot(vx, vy) / r - 0.4) / 0.5, 0.0, 1.0)
                r = r * (1.0 + lobe * wave * edge * edge * (3.0 - 2.0 * edge))
            d = np.hypot(vx, vy) / r
            acc += np.exp(smooth * np.sqrt(np.clip(1.0 - d * d, 0.0, 1.0)))
    # 9 近傍すべてが 0 のとき 0 になるよう log(9) を引く
    return np.log(acc / 9.0) / smooth


def build_moko(size, seed, recipe):
    """戻り値: 高さ 0..1"""
    rng = np.random.default_rng(seed + 1000)
    # ワープは全段で共通。周期 4 の Perlin なので継ぎ目なし。値はおおよそ ±1
    warp_x = fbm(perlin, size, 4, 3, rng)
    warp_y = fbm(perlin, size, 4, 3, rng)
    height = np.zeros((size, size))
    for period, weight in recipe["octaves"]:
        # ワープ量はセル単位で全段同じにする。細かい段ほど大きくずらすと、小さいこぶが引き伸ばされて放射状の筋になる
        height += weight * domes(size, period, recipe, rng, warp_x, warp_y)
    height = (height - height.min()) / max(np.ptp(height), 1e-6)

    if recipe["billow"] > 0.0:
        # |Perlin| は 0 付近で折れて谷になり、膨らみの形が決まっていない。反転して「盛り上がり」として混ぜる
        # ただの |x| だと折れ目が角ばった尾根になり、くしゃくしゃの紙に見える。sqrt(x² + ε²) で丸める
        billow = fbm(lambda s, p, r: np.sqrt(perlin(s, p, r) ** 2 + BILLOW_SOFTNESS ** 2), size, 4, 5, rng)
        billow = 1.0 - (billow - billow.min()) / max(np.ptp(billow), 1e-6)
        height = (1.0 - recipe["billow"]) * height + recipe["billow"] * billow
        height = (height - height.min()) / max(np.ptp(height), 1e-6)
    return height


def moko_render(shape, moko, tile):
    """雲の形にもこもこを当てた見え方。シェーダで実装する式の手本（下から見上げた遠景の雲）"""
    pv = PREVIEW
    size = shape.shape[0]
    idx = (np.arange(size) * tile) % moko.shape[0]
    m = moko[np.ix_(idx, idx)]

    d = (shape + pv["moko_amount"] * (m - 0.5) - pv["coverage"]) * pv["sharpness"]
    alpha = np.clip(d, 0.0, 1.0)
    alpha = alpha * alpha * (3.0 - 2.0 * alpha)

    # 大きい影：太陽側へ数歩サンプルして、通り抜ける密度から透過率を出す（光側マーチの 2D 版）
    sx, sy = pv["sun_step"]
    optical = np.zeros_like(d)
    for i in range(1, pv["sun_steps"] + 1):
        optical += np.clip(np.roll(d, (-sy * i, -sx * i), axis=(0, 1)), 0.0, None)
    macro = np.exp(-optical * pv["density"])

    # 小さい影：こぶの太陽側が明るく、谷が灰色
    mx, my = pv["micro_step"]
    # 比べる距離は画面（雲の形）側のテクセルで決める。もこもこの量が 0 なら小さい影も消す
    slope = m - np.roll(m, (-my, -mx), axis=(0, 1))
    micro = np.clip(0.55 + slope * pv["micro_gain"] * (pv["moko_amount"] / 0.3), 0.0, 1.0)

    light = np.clip(0.35 * macro + 0.65 * np.sqrt(macro) * (0.4 + 0.96 * micro), 0.0, 1.0)
    lit = np.array(pv["lit"])
    shadow = np.array(pv["shadow"])
    color = shadow + (lit - shadow) * light[..., None]
    color += np.array(pv["rim"]) * (4.0 * alpha * (1.0 - alpha))[..., None]

    sky = np.array(pv["sky"])
    out = sky * (1.0 - alpha[..., None]) + color * alpha[..., None]
    return np.clip(out, 0.0, 1.0)


def moko_preview(moko, tile, path):
    """上=高さ（2x2 にタイルして継ぎ目の確認） / 下=今の雲の形に当てた見え方（左=なし, 右=あり）"""
    shape, _ = build(512, 1)
    tiled = np.dstack([np.tile(moko, (2, 2))] * 3)
    tiled = np.array(Image.fromarray(np.round(tiled * 255).astype(np.uint8)).resize((1034, 1034)))
    tiled = tiled[:517].astype(np.float64) / 255.0

    keep = PREVIEW["moko_amount"]
    PREVIEW["moko_amount"] = 0.0
    without = moko_render(shape, moko, tile)
    PREVIEW["moko_amount"] = keep
    with_moko = moko_render(shape, moko, tile)

    def crop(img):
        part = np.round(img[0:256, 0:256] ** (1.0 / 2.2) * 255.0).astype(np.uint8)
        return np.array(Image.fromarray(part).resize((512, 512), Image.LANCZOS)).astype(np.float64) / 255.0

    gap = np.ones((512, 10, 3))
    bottom = np.concatenate([crop(without), gap, crop(with_moko)], axis=1)
    image = np.concatenate([tiled[:, :1034], np.ones((10, 1034, 3)), bottom], axis=0)
    Image.fromarray(np.round(np.clip(image, 0.0, 1.0) * 255.0).astype(np.uint8), "RGB").save(path)


def moko_to_png(moko, path):
    Image.fromarray(np.round(moko * 255.0).astype(np.uint8), "L").save(path)


def to_png(shape, mask, path):
    rgb = np.zeros(shape.shape + (3,), dtype=np.uint8)
    rgb[..., 0] = np.round(shape * 255.0).astype(np.uint8)
    rgb[..., 1] = np.round(mask * 255.0).astype(np.uint8)
    Image.fromarray(rgb, "RGB").save(path)


def main(argv):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--size", type=int, default=512)
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--preview", type=Path, help="中間 PNG をこのパスにも保存する（--moko では確認用の画像）")
    parser.add_argument("--moko", action="store_true", help="形/マスクの代わりに、もこもこの高さを焼く")
    parser.add_argument("--recipe", default="large_soft", choices=sorted(MOKO_RECIPES), help="--moko のレシピ")
    parser.add_argument("--tile", type=int, default=3, help="--moko のプレビューで、雲の形1枚の間にもこもこを何回繰り返すか")
    parser.add_argument("--no-dds", action="store_true", help="DDS を書かずにプレビューだけ作る")
    args = parser.parse_args(argv)

    if not args.no_dds and not TEXCONV.exists():
        print(f"[ERROR] {TEXCONV} が見つかりません（Project/ をカレントにして実行してください）")
        return 1

    output = MOKO_OUTPUT if args.moko else OUTPUT
    output.parent.mkdir(parents=True, exist_ok=True)

    with tempfile.TemporaryDirectory() as tmp:
        png = Path(tmp) / (output.stem + ".png")
        if args.moko:
            moko = build_moko(args.size, args.seed, MOKO_RECIPES[args.recipe])
            moko_to_png(moko, png)
            if args.preview:
                moko_preview(moko, args.tile, args.preview)
                print(f"preview: {args.preview}")
        else:
            shape, mask = build(args.size, args.seed)
            to_png(shape, mask, png)
            if args.preview:
                shutil.copyfile(png, args.preview)
                print(f"preview: {args.preview}")

        if args.no_dds:
            return 0

        cmd = [
            str(TEXCONV), "-nologo", "-y",
            "-f", "BC4_UNORM" if args.moko else "BC5_UNORM",
            "-dx10",          # pack_assets は DXT10 ヘッダ前提
            "-m", "0",        # ミップを最後まで作る（水平線のちらつき対策）
            "-wrap",          # ミップ生成時に端を折り返す（タイルの継ぎ目を出さない）
            "-o", str(output.parent),
            str(png),
        ]
        result = subprocess.run(cmd, capture_output=True, text=True)
        if result.returncode != 0:
            print(f"[ERROR] texconv failed (exit {result.returncode})")
            print(result.stdout)
            print(result.stderr)
            return 1

    print(f"wrote {output} ({args.size}x{args.size}, seed={args.seed})")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
