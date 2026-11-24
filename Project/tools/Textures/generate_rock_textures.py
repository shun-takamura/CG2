"""
ボス必殺技の岩のテクスチャ（外側・断面）を手続き的に生成する。どれも上下左右にタイル可能。
三平面投影（triplanar）で貼る前提なので UV の継ぎ目を気にしなくてよい。

出力（--out のフォルダへ）:
  RockExterior_BaseColor.png / RockExterior_NormalMap.png … 黒っぽい多孔質の岩肌
  RockInterior_BaseColor.png / RockInterior_NormalMap.png … 割れたばかりの明るめの断面
法線マップは OpenGL 規約（G = テクスチャの上方向が +）。

使い方:
  python Project/Tools/Textures/generate_rock_textures.py --out Project/Assets/Textures/Rocks
"""
import argparse
import os

import numpy as np
from PIL import Image


def value_noise(size, cells, rng):
	"""周期 cells の格子の値ノイズ（タイル可能）。戻り値は 0〜1"""
	cells = max(1, min(cells, size))
	lat = rng.random((cells, cells), dtype=np.float32)
	x = np.arange(size, dtype=np.float32) * cells / size
	i0 = np.floor(x).astype(np.int64)
	f = x - i0
	i1 = (i0 + 1) % cells
	u = f * f * (3.0 - 2.0 * f)
	a = lat[np.ix_(i0, i0)]
	b = lat[np.ix_(i0, i1)]
	c = lat[np.ix_(i1, i0)]
	d = lat[np.ix_(i1, i1)]
	uy = u[:, None]
	ux = u[None, :]
	return (a * (1 - ux) + b * ux) * (1 - uy) + (c * (1 - ux) + d * ux) * uy


def fbm(size, base_cells, octaves, rng, gain=0.5):
	"""値ノイズを重ねたフラクタルノイズ。戻り値は 0〜1"""
	total = np.zeros((size, size), dtype=np.float32)
	amp = 1.0
	amp_sum = 0.0
	for k in range(octaves):
		total += value_noise(size, base_cells * (2 ** k), rng) * amp
		amp_sum += amp
		amp *= gain
	return total / amp_sum


def worley(size, cells, rng):
	"""周期 cells のセルノイズ。最も近い点と 2 番目に近い点までの距離（セル単位）を返す"""
	pts = rng.random((cells, cells, 2), dtype=np.float32)
	p = (np.arange(size, dtype=np.float32) + 0.5) * cells / size
	ci = np.floor(p).astype(np.int64)
	f1 = np.full((size, size), 1e9, dtype=np.float32)
	f2 = np.full((size, size), 1e9, dtype=np.float32)
	for dy in (-1, 0, 1):
		for dx in (-1, 0, 1):
			cy = (ci + dy) % cells
			cx = (ci + dx) % cells
			jit = pts[np.ix_(cy, cx)]  # (H, W, 2)
			qy = (ci + dy)[:, None] + jit[..., 1]
			qx = (ci + dx)[None, :] + jit[..., 0]
			d = np.sqrt((qy - p[:, None]) ** 2 + (qx - p[None, :]) ** 2)
			f2 = np.where(d < f1, f1, np.minimum(f2, d))
			f1 = np.minimum(f1, d)
	return f1, f2


def smoothstep(e0, e1, x):
	t = np.clip((x - e0) / (e1 - e0), 0.0, 1.0)
	return t * t * (3.0 - 2.0 * t)


def lerp_color(c0, c1, t):
	c0 = np.asarray(c0, dtype=np.float32)
	c1 = np.asarray(c1, dtype=np.float32)
	return c0 + (c1 - c0) * t[..., None]


def normal_from_height(h, strength):
	"""高さから OpenGL 規約の法線マップ（0〜1）を作る。端は回り込み（タイル可能）"""
	dhdx = (np.roll(h, -1, axis=1) - np.roll(h, 1, axis=1)) * 0.5 * strength
	# 画像の行は上から下へ並ぶので、テクスチャの上方向の傾きは「上の行 − 下の行」
	dhdy = (np.roll(h, 1, axis=0) - np.roll(h, -1, axis=0)) * 0.5 * strength
	n = np.stack([-dhdx, -dhdy, np.ones_like(h)], axis=-1)
	n /= np.linalg.norm(n, axis=-1, keepdims=True)
	return n * 0.5 + 0.5


def save_rgb(path, rgb):
	img = (np.clip(rgb, 0.0, 1.0) * 255.0 + 0.5).astype(np.uint8)
	Image.fromarray(img, "RGB").save(path)
	print("wrote", path)


def make_exterior(size, rng):
	"""黒っぽい多孔質の岩肌（参考：鬼武者の浮く大岩）"""
	large = fbm(size, 4, 6, rng)
	mid = fbm(size, 16, 4, rng)
	dust_n = fbm(size, 8, 4, rng)
	pore_f1, _ = worley(size, 64, rng)
	pore_mask = smoothstep(0.45, 0.65, value_noise(size, 32, rng))
	pore = np.clip((0.28 - pore_f1) / 0.28, 0.0, 1.0) ** 1.5 * pore_mask
	crack_f1, crack_f2 = worley(size, 6, rng)
	crack = np.clip(1.0 - (crack_f2 - crack_f1) / 0.035, 0.0, 1.0) * smoothstep(0.35, 0.6, mid)

	t = np.clip(large * 1.3 - 0.15 + (mid - 0.5) * 0.4, 0.0, 1.0)
	col = lerp_color((0.06, 0.06, 0.065), (0.25, 0.24, 0.225), t)
	col *= (1.0 - 0.6 * pore - 0.55 * crack)[..., None]
	# 窪みにたまった砂ぼこり（画面の黄色い砂嵐となじませる）
	dust = smoothstep(0.62, 0.78, dust_n) * 0.22
	col = col * (1.0 - dust[..., None]) + np.array([0.42, 0.36, 0.26], np.float32) * dust[..., None]

	height = large * 0.5 + mid * 0.35 - pore * 0.35 - crack * 0.45
	return col, normal_from_height(height, strength=size * 0.012)


def make_interior(size, rng):
	"""割れたばかりの断面（外側より明るく、鉱物の粒と筋が見える）"""
	base = fbm(size, 8, 6, rng)
	grain_dark = value_noise(size, 256, rng)
	grain_light = value_noise(size, 256, rng)
	vein_n = fbm(size, 3, 5, rng)
	cf1, _ = worley(size, 24, rng)

	col = lerp_color((0.30, 0.29, 0.27), (0.50, 0.48, 0.44), np.clip(base * 1.4 - 0.2, 0.0, 1.0))
	dark = smoothstep(0.80, 0.88, grain_dark)
	light = smoothstep(0.86, 0.93, grain_light)
	vein = np.clip(1.0 - np.abs(vein_n - 0.5) / 0.012, 0.0, 1.0) * 0.6
	col = col * (1.0 - dark[..., None]) + np.array([0.11, 0.11, 0.11], np.float32) * dark[..., None]
	col = col * (1.0 - light[..., None]) + np.array([0.76, 0.74, 0.69], np.float32) * light[..., None]
	col = col * (1.0 - vein[..., None]) + np.array([0.64, 0.61, 0.55], np.float32) * vein[..., None]

	# 貝殻状の割れ口のゆるい凹凸 + 粒のざらつき
	height = base * 0.45 + cf1 * 0.35 + (grain_dark - 0.5) * 0.08 - vein * 0.1
	return col, normal_from_height(height, strength=size * 0.01)


def main():
	ap = argparse.ArgumentParser()
	ap.add_argument("--out", required=True)
	ap.add_argument("--size", type=int, default=1024)
	ap.add_argument("--seed", type=int, default=7)
	args = ap.parse_args()
	os.makedirs(args.out, exist_ok=True)
	rng = np.random.default_rng(args.seed)

	col, nrm = make_exterior(args.size, rng)
	save_rgb(os.path.join(args.out, "RockExterior_BaseColor.png"), col)
	save_rgb(os.path.join(args.out, "RockExterior_NormalMap.png"), nrm)

	col, nrm = make_interior(args.size, rng)
	save_rgb(os.path.join(args.out, "RockInterior_BaseColor.png"), col)
	save_rgb(os.path.join(args.out, "RockInterior_NormalMap.png"), nrm)


if __name__ == "__main__":
	main()
