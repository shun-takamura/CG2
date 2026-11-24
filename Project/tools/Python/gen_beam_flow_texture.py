"""ビームの中身（流れる繊維の模様）のテクスチャを焼いて Resources/Textures/Effect/ に出力する。

エフェクトの Ribbon（Beam）に貼る前提のレイアウト:
    U（横）＝ビームの長さ方向。左右がつながる（WRAP でくり返し、UV スクロールで根元→先へ流す）
    V（縦）＝ビームの断面。上下の端＝白く光る縁、真ん中＝暗めの青に明るい繊維の筋

模様は「長さ方向に引き伸ばしたノイズ」をリッジ化（1-|2n-1| を尖らせる）して細い繊維にしたもの。
ノイズは周波数空間で異方性ガウスをかけたホワイトノイズなので、上下左右とも周期的につながる。
少しだけ低周波のノイズで V をゆがませ、繊維をうねらせる。

色はテクスチャに焼き込む（Ribbon の色は白のまま使う。Ribbon の色で掛け算すれば色味を変えられる）。
出力は BC7_UNORM_SRGB＋ミップ。PNG を Assets/ に置くと cook_assets.py が上書きするので、中間 PNG は一時フォルダ。

    py tools\\Python\\gen_beam_flow_texture.py                     # beam_flow_00.dds / beam_flow_01.dds
    py tools\\Python\\gen_beam_flow_texture.py --preview flow.png  # 確認用 PNG も残す（00 の分）
    py tools\\Python\\gen_beam_flow_texture.py --no-dds --preview flow.png
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
OUTPUT_DIR = Path("Resources/Textures/Effect")

# 2 枚の作り分け（00 は太めのゆったりした繊維、01 は細かく速い繊維。速さの違う 2 層で重ねる用）
RECIPES = {
    "beam_flow_00": dict(seed=11, stretch=18.0, fiber_freq=22.0, sharp=7.0, warp=0.025, coarse=0.55),
    "beam_flow_01": dict(seed=29, stretch=24.0, fiber_freq=40.0, sharp=10.0, warp=0.015, coarse=0.45),
}

# 色（sRGB 0..1）
EDGE_COLOR = np.array([1.00, 1.00, 1.00])   # 縁の白
GLOW_COLOR = np.array([0.55, 0.85, 1.00])   # 縁のすぐ内側の水色
BODY_COLOR = np.array([0.03, 0.10, 0.35])   # 真ん中の暗い青
FIBER_COLOR = np.array([0.45, 0.80, 1.00])  # 繊維の水色（明るい所は白へ寄る）


def periodic_noise(h, w, sigma_u, sigma_v, rng):
    """周期的につながる異方性ノイズ（0..1 に正規化）。sigma は周波数空間の幅（大きいほど細かい）"""
    white = rng.standard_normal((h, w))
    fu = np.fft.fftfreq(w)[None, :] * w   # 横方向の周波数（周期の数）
    fv = np.fft.fftfreq(h)[:, None] * h   # 縦方向の周波数
    filt = np.exp(-0.5 * ((fu / sigma_u) ** 2 + (fv / sigma_v) ** 2))
    n = np.real(np.fft.ifft2(np.fft.fft2(white) * filt))
    n -= n.min()
    n /= max(n.max(), 1e-8)
    return n


def sample_wrap_v(img, v_offset_px):
    """各画素を縦に v_offset_px ずらして読む（縦は周期で折り返す・線形補間）"""
    h, w = img.shape
    ys = (np.arange(h)[:, None] + v_offset_px) % h
    y0 = np.floor(ys).astype(int)
    t = ys - y0
    y1 = (y0 + 1) % h
    cols = np.arange(w)[None, :].repeat(h, axis=0)
    return img[y0, cols] * (1.0 - t) + img[y1, cols] * t


def smoothstep(a, b, x):
    t = np.clip((x - a) / (b - a), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def make_flow(width, height, seed, stretch, fiber_freq, sharp, warp, coarse):
    rng = np.random.default_rng(seed)
    # 繊維：縦は細かく（fiber_freq）、横は stretch 倍ゆったり → 長さ方向に伸びた筋
    base = periodic_noise(height, width, fiber_freq / stretch, fiber_freq, rng)
    # うねり：低周波のノイズで V をずらす
    wobble = periodic_noise(height, width, 3.0, 2.0, rng) - 0.5
    base = sample_wrap_v(base, wobble * warp * height)
    # リッジ化：0.5 付近（ノイズの谷と山の境目）を細い明るい線にする
    ridge = 1.0 - np.abs(base * 2.0 - 1.0)
    fibers = ridge ** sharp
    # 太い流れ：低周波の明暗をかけて、繊維の濃い所・薄い所を作る
    clumps = periodic_noise(height, width, 2.0, 4.0, rng)
    fibers = fibers * (1.0 - coarse + coarse * 2.0 * clumps)
    fibers = np.clip(fibers, 0.0, 1.0)

    # 断面：d=0 中央、1 縁
    v = (np.arange(height) + 0.5) / height
    d = (np.abs(v - 0.5) * 2.0)[:, None]
    edge = smoothstep(0.70, 0.92, d)              # 白い縁
    glow = smoothstep(0.40, 0.78, d) * (1.0 - edge)  # 縁の内側の水色
    # 真ん中ほど繊維がよく見える（縁は白で埋まる）
    body = BODY_COLOR[None, None, :] + FIBER_COLOR[None, None, :] * fibers[..., None] * 1.2
    # 繊維の明るい所は白へ寄せる
    hot = np.clip(fibers - 0.6, 0.0, 1.0)[..., None] * 1.5
    body = body * (1.0 - hot) + EDGE_COLOR[None, None, :] * hot
    col = body * (1.0 - glow[..., None]) + GLOW_COLOR[None, None, :] * glow[..., None]
    col = col * (1.0 - edge[..., None]) + EDGE_COLOR[None, None, :] * edge[..., None]
    col = np.clip(col, 0.0, 1.0)
    alpha = np.ones((height, width))
    return np.dstack([col, alpha])


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--width", type=int, default=512, help="長さ方向の画素数")
    ap.add_argument("--height", type=int, default=256, help="断面方向の画素数")
    ap.add_argument("--preview", type=str, default="", help="00 の確認用 PNG の出力先")
    ap.add_argument("--no-dds", action="store_true", help="DDS を書かない（見た目だけ確認）")
    args = ap.parse_args(argv)

    with tempfile.TemporaryDirectory() as tmp:
        for name, recipe in RECIPES.items():
            rgba = make_flow(args.width, args.height, **recipe)
            png = Path(tmp) / f"{name}.png"
            Image.fromarray((rgba * 255.0 + 0.5).astype(np.uint8), "RGBA").save(png)
            if args.preview and name.endswith("00"):
                shutil.copyfile(png, args.preview)
                print(f"preview: {args.preview}")
            if args.no_dds:
                continue
            cmd = [
                str(TEXCONV), "-nologo", "-y",
                "-f", "BC7_UNORM_SRGB",
                "-srgb",          # PNG を sRGB として読み、そのまま sRGB で書く（色を持ち上げない）
                "-dx10",          # pack_assets は DXT10 ヘッダ前提
                "-m", "0",
                "-wrap",          # 長さ方向は折り返してミップを作る
                "-o", str(OUTPUT_DIR),
                str(png),
            ]
            result = subprocess.run(cmd, capture_output=True, text=True)
            if result.returncode != 0:
                print(f"[ERROR] texconv failed (exit {result.returncode})")
                print(result.stdout)
                print(result.stderr)
                return 1
            print(f"wrote {OUTPUT_DIR / (name + '.dds')} ({args.width}x{args.height})")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
