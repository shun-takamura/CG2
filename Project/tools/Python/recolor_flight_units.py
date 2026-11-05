"""
飛行ユニット（防御・機動）の BaseColor を、自機（PlayerModel）の色合いに寄せる（2026-11-05 ユーザー）。

青・金のそれぞれで、色相は自機の中心へ寄せ、彩度と明るさは分布（分位）を自機の分布に合わせる。
水色の光る筋はそのまま。自機のテクスチャは見本として読むだけで書き換えない。

元の PNG は最初に走らせたときだけ Project/Assets_Original/ に写し、いつもそこから変換する
（Assets の外なので cook されない。値を変えて何度走らせても重ねがけにならない）。
--cook を付けると、変換した PNG だけを cook_assets.py と同じ方法で DDS にする（全体の cook は関係ないシーンまで作り直すので避ける）。

使い方（Project/ で）:
  py tools/Python/recolor_flight_units.py --preview   # 書き換えずに比べる画像だけ（tools/Python/recolor_flight_units_preview.png）
  py tools/Python/recolor_flight_units.py --cook      # Assets の PNG を書き換えて DDS まで
"""
import argparse
import shutil
import sys
from pathlib import Path

import numpy as np
from PIL import Image

PROJECT_DIR = Path(__file__).resolve().parents[2]
ASSETS_DIR = PROJECT_DIR / "Assets"
ORIGINAL_DIR = PROJECT_DIR / "Assets_Original"
TARGET_DIRS = [
    ASSETS_DIR / "Models/Weapons/FlightUnit/Defense/Export",
    ASSETS_DIR / "Models/Weapons/FlightUnit/Mobility/Export",
]
REFERENCE_DIR = ASSETS_DIR / "Models/Player/PlayerModel/textures"
PREVIEW_PATH = Path(__file__).resolve().parent / "recolor_flight_units_preview.png"

# ===== 調整値 =====
# 色の組：色相の窓 [度]・色相をどれだけ自機へ寄せるか（1 で中心をそろえる）・彩度/明るさを分布に合わせる強さ（0..1）
CLASSES = {
    "blue": {"hue": (195.0, 262.0), "hue_follow": 1.0, "sat_follow": 1.0, "value_follow": 0.85},
    "gold": {"hue": (15.0, 68.0), "hue_follow": 1.0, "sat_follow": 1.0, "value_follow": 0.85},
}
MIN_SATURATION = 0.06   # これより灰色の画素は色の組に入れない
QUANTILES = np.linspace(0.0, 1.0, 51)


def rgb_to_hsv(rgb: np.ndarray) -> np.ndarray:
    r, g, b = rgb[..., 0], rgb[..., 1], rgb[..., 2]
    mx = rgb.max(-1)
    d = mx - rgb.min(-1)
    h = np.zeros_like(mx)
    m = d > 1e-6
    rr = m & (mx == r)
    gg = m & (mx == g) & ~rr
    bb = m & ~rr & ~gg
    h[rr] = ((g - b)[rr] / d[rr]) % 6
    h[gg] = (b - r)[gg] / d[gg] + 2
    h[bb] = (r - g)[bb] / d[bb] + 4
    s = np.where(mx > 1e-6, d / np.maximum(mx, 1e-6), 0.0)
    return np.stack([h * 60.0, s, mx], -1)


def hsv_to_rgb(hsv: np.ndarray) -> np.ndarray:
    h, s, v = (hsv[..., 0] % 360.0) / 60.0, hsv[..., 1], hsv[..., 2]
    i = np.floor(h).astype(int) % 6
    f = h - np.floor(h)
    p, q, t = v * (1 - s), v * (1 - s * f), v * (1 - s * (1 - f))
    out = np.zeros(hsv.shape)
    for k, (a, b, c) in enumerate([(v, t, p), (q, v, p), (p, v, t), (p, q, v), (t, p, v), (v, p, q)]):
        sel = i == k
        out[..., 0][sel], out[..., 1][sel], out[..., 2][sel] = a[sel], b[sel], c[sel]
    return out


def class_weight(hsv: np.ndarray, lo: float, hi: float) -> np.ndarray:
    """色相の窓（端は 10 度でなめらか）× 彩度（灰色は入れない）"""
    h, s = hsv[..., 0], hsv[..., 1]
    soft = 10.0
    w = np.minimum(np.clip((h - (lo - soft)) / soft, 0, 1), np.clip(((hi + soft) - h) / soft, 0, 1))
    return w * np.clip((s - MIN_SATURATION) / 0.06, 0, 1)


def load(path: Path, size=None) -> np.ndarray:
    im = Image.open(path).convert("RGB")
    if size:
        im = im.resize((size, size))
    return np.asarray(im, dtype=np.float64) / 255.0


def save(path: Path, rgb: np.ndarray) -> None:
    Image.fromarray(np.round(rgb * 255.0).astype(np.uint8)).save(path)


def class_quantiles(paths, lo, hi):
    """色の組の画素の色相（中央値）と彩度・明るさの分位"""
    pixels = []
    for p in paths:
        hsv = rgb_to_hsv(load(p, 256)).reshape(-1, 3)
        pixels.append(hsv[class_weight(hsv, lo, hi) > 0.5])
    a = np.concatenate(pixels)
    return np.median(a[:, 0]), np.quantile(a[:, 1], QUANTILES), np.quantile(a[:, 2], QUANTILES)


def build_mapping(sources, reference):
    mapping = {}
    for name, c in CLASSES.items():
        mapping[name] = (class_quantiles(sources, *c["hue"]), class_quantiles(reference, *c["hue"]))
    return mapping


def recolor(rgb: np.ndarray, mapping) -> np.ndarray:
    hsv = rgb_to_hsv(rgb)
    out = hsv.copy()
    for name, c in CLASSES.items():
        (src_h, src_s, src_v), (ref_h, ref_s, ref_v) = mapping[name]
        w = class_weight(hsv, *c["hue"])
        h, s, v = hsv[..., 0], hsv[..., 1], hsv[..., 2]
        nh = h + (ref_h - src_h) * c["hue_follow"]
        # 分位で写す（ユニットで上から x% の彩度・明るさ → 自機で上から x% の値）
        ns = s + (np.interp(s, src_s, ref_s) - s) * c["sat_follow"]
        nv = v + (np.interp(v, src_v, ref_v) - v) * c["value_follow"]
        out[..., 0] = out[..., 0] * (1 - w) + nh * w
        out[..., 1] = out[..., 1] * (1 - w) + ns * w
        out[..., 2] = out[..., 2] * (1 - w) + nv * w
    return np.clip(hsv_to_rgb(out), 0, 1)


def original_of(path: Path) -> Path:
    return ORIGINAL_DIR / path.relative_to(ASSETS_DIR)


def cook(paths) -> None:
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    import cook_assets as ca
    cache = ca.load_cache()
    for src in paths:
        # cook_assets は Project/ から見た相対の Assets で組み立てるので、それに合わせる
        task = ca.classify(ca.ASSETS_DIR / src.relative_to(ASSETS_DIR))
        if ca.convert_png_to_dds(task):
            cache[task.src.as_posix()] = task.src.stat().st_mtime
            print(f"[dds] {task.dst}")
    ca.save_cache(cache)


def main() -> int:
    parser = argparse.ArgumentParser(description="飛行ユニット（防御・機動）の BaseColor を自機の色合いに寄せる")
    parser.add_argument("--preview", action="store_true", help="書き換えずに比べる画像だけを出す")
    parser.add_argument("--cook", action="store_true", help="書き換えた PNG を DDS にする")
    args = parser.parse_args()

    targets = sorted(p for d in TARGET_DIRS for p in d.glob("*_BaseColor.png"))
    reference = sorted(REFERENCE_DIR.glob("*_BaseColor.png"))
    if not targets or not reference:
        print("[ERROR] 変換する PNG か見本の PNG がありません", file=sys.stderr)
        return 1

    # 元の PNG を一度だけ写す（以後はいつもここから変換する）
    for src in targets:
        backup = original_of(src)
        if not backup.exists():
            backup.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(src, backup)
            print(f"[backup] {backup.relative_to(PROJECT_DIR)}")

    mapping = build_mapping([original_of(p) for p in targets], reference)
    for name, ((sh, ss, sv), (rh, rs, rv)) in mapping.items():
        print(f"{name}: hue {sh:.0f} -> {rh:.0f}  sat(median) {ss[25]:.2f} -> {rs[25]:.2f}  value(median) {sv[25]:.2f} -> {rv[25]:.2f}")

    if args.preview:
        tiles = []
        for src in targets[:4]:
            before = load(original_of(src), 384)
            tiles.append(np.concatenate([before, recolor(before, mapping)], 1))
        ref = [load(p, 384) for p in reference[:2]]
        tiles.append(np.concatenate(ref, 1))
        save(PREVIEW_PATH, np.concatenate(tiles, 0))
        print(f"[preview] {PREVIEW_PATH}")
        return 0

    for src in targets:
        save(src, recolor(load(original_of(src)), mapping))
        print(f"[recolor] {src.relative_to(PROJECT_DIR)}")
    if args.cook:
        cook(targets)
    return 0


if __name__ == "__main__":
    sys.exit(main())
