"""雲海のレイマーチ（CloudRaymarch.hlsli のゼノブレ式の面）を CPU で 1 枚描いて PNG に出す。

シェーダを直す → ビルド → 実機、の往復を減らすための確認用。式は CloudRaymarch.hlsli と同じ
（CloudOver / SurfaceHeight / TraceSurface＝上から見下ろす経路 / SurfaceLight）。式を変えたら両方を直して見比べる。

  - ノイズは gen_cloud_noise.py と同じ生成（seed 1・512²）を numpy で作る。DDS は読まない（BC5/BC4 を解かないため）
    初回だけ数十秒かかり、以降は一時フォルダのキャッシュを使う
  - ミップは距離から選ぶ（GPU の lodScale と同じ考え方）
  - 降下の縦穴も描く（レールの 48〜66 秒、StagePlayScene と同じ）。--at-sec でレール上のカメラ（接線向き）から見られる
  - 描かないもの：B'（入口・出口）・リム。空は青空のおおまかな近似
  - パラメータは Resources/Json/Tuning/StagePlay.json の raymarchClouds があれば読み、無ければ C++ の既定値

    py tools\\Python\\preview_cloud_raymarch.py                       # 開始位置（Y=900）から水平に見る
    py tools\\Python\\preview_cloud_raymarch.py --pitch 10 --out down.png
    py tools\\Python\\preview_cloud_raymarch.py --set coverage=0.3 mokoScale=400 --out cov03.png
"""
import argparse
import json
import math
import sys
import tempfile
from pathlib import Path

import numpy as np
from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))
import gen_cloud_noise as gen  # noqa: E402

PROJECT_DIR = Path(__file__).resolve().parents[2]
TUNING_PATH = PROJECT_DIR / "Resources" / "Json" / "Tuning" / "StagePlay.json"
SCENE_PATH = PROJECT_DIR / "Resources" / "Json" / "Scenes" / "StagePlay.json"
CACHE_PATH = Path(tempfile.gettempdir()) / "preview_cloud_raymarch_noise_soft.npz"

# CloudRaymarcher::Params の既定値（ゼノブレ式の面で使う分だけ）
DEFAULTS = dict(
    top=850.0, bottom=300.0, bottomFade=15.0, seaDepth=160.0, noiseFloor=0.3, coverage=0.22, sharpness=3.0,
    shapeScale=1800.0, mokoScale=500.0, mokoWeight=0.55, maskScale=9000.0, maskStrength=0.4,
    steps=48, firstStep=4.0, maxDistance=20000.0, probeLength=30.0,
    normalEps=12.0, wrap=0.3, lightStep=18.0, density=1.2, valley=0.7, valleyPow=0.6, ambient=0.15,
    litColor=[1.0, 0.99, 0.97], shadowColor=[0.30, 0.38, 0.55],
    traceSlope=0.35, traceEpsilon=0.5, xeno=0.0, hv=0.0, accumulate=1.0, refStep=60.0, sunElevation=0.0, detailScale=500.0, detailWeight=0.0, softDistance=3000.0, lightTaps=2.0, grazingMin=0.05, grazingPower=1.0, traceRefine=0.0, detailFadeStart=600.0, detailFadeEnd=2500.0, detailFadeLod=2.0, mokoLodBias=0.0, mokoNormal=1.0, mokoHeight=0.0, linearHeight=0.0, mokoValleyLow=0.0, mokoValleyHigh=0.0,
    shaftRadius=60.0, shaftWallDepth=80.0, shaftWallBlend=25.0, shaftDarkness=0.6, undersideBright=0.45,
    capTop=60.0, capBottom=10.0, floorDepth=25.0, exitZoneInner=0.0, exitZoneOuter=100.0, capDepth=60.0, shaftNoiseScale=0.25, volume=1.0,
    volumeZoneInner=40.0, volumeZoneOuter=120.0, volumeSteps=48, volumeRange=300.0, volumeShaftWeight=0.2, extinction=0.015,
    erosion=0.5, erosionSharp=2.0, lightExtinction=4.0, erosionScale=100.0, powder=6.0, ambientStrength=0.15, volumeLightStep=25.0, rimG=0.6,
    hazeStart=3000.0, hazeEnd=15000.0, hazeMax=0.5, fadeStart=14000.0, fadeEnd=20000.0,
)
# ステージの太陽（Tuning/StagePlay.json の SkyAboveClouds の光の向きの逆＝太陽へ向かう向き）
SUN_DIRECTION = np.array([-0.191, 0.815, -0.547])
# 空の水平線より下の色（線形）。タイトルの青空 cubemap は下半分も濃い青なので、透けると雲が青く見える
SKY_BELOW = [0.20, 0.42, 0.80]
# もこもこの陰影の強さの基準にするノイズの値（ノイズ 0.3〜1 の真ん中あたり）。シェーダの kMokoNoiseRef と同じ
MOKO_NOISE_REF = 0.65


# ---------------------------------------------------------------------------
# ノイズとサンプリング
# ---------------------------------------------------------------------------

def load_noise():
    if CACHE_PATH.exists():
        d = np.load(CACHE_PATH)
        return d["shape"], d["mask"], d["moko"]
    print("ノイズを生成中（初回のみ）...", flush=True)
    shape, mask = gen.build(512, 1)
    moko = gen.build_moko(512, 1, gen.MOKO_RECIPES["large_soft_smooth"])  # cloud_moko_soft.dds と同じ
    np.savez(CACHE_PATH, shape=shape, mask=mask, moko=moko)
    return shape, mask, moko


def pyramid(tex):
    levels = [tex]
    while levels[-1].shape[0] > 1:
        t = levels[-1]
        levels.append(0.25 * (t[0::2, 0::2] + t[1::2, 0::2] + t[0::2, 1::2] + t[1::2, 1::2]))
    return levels


def sample(tex, u, v):
    """bilinear・wrap。u, v は UV（1 で 1 周）"""
    n = tex.shape[0]
    x = u * n - 0.5
    y = v * n - 0.5
    x0 = np.floor(x).astype(np.int64)
    y0 = np.floor(y).astype(np.int64)
    fx = x - x0
    fy = y - y0
    x0 %= n
    y0 %= n
    x1 = (x0 + 1) % n
    y1 = (y0 + 1) % n
    a = tex[y0, x0] * (1 - fx) + tex[y0, x1] * fx
    b = tex[y1, x0] * (1 - fx) + tex[y1, x1] * fx
    return a * (1 - fy) + b * fy


class Noise:
    def __init__(self, pixel_angle):
        shape, mask, moko = load_noise()
        self.levels = {"shape": pyramid(shape), "mask": pyramid(mask), "moko": pyramid(moko)}
        # 傾き（gen_cloud_noise.py --grad と同じ中心差分。UV 1 あたり）。法線はこれを読む
        for name, height in (("shape", shape), ("moko", moko)):
            du, dv = gen.gradient_uv(height)
            self.levels[name + "_du"] = pyramid(du)
            self.levels[name + "_dv"] = pyramid(dv)
        self.pixel_angle = pixel_angle

    def sample(self, name, x, z, scale, dist, bias=0.0):
        """距離 dist [m] とノイズ 1 枚の大きさ scale [m] からミップを選ぶ（レベル間は線形）。bias で粗い側へずらす"""
        levels = self.levels[name]
        texels = dist * self.pixel_angle * levels[0].shape[0] / scale
        lod = np.clip(np.log2(np.maximum(texels, 1.0)) + bias, 0, len(levels) - 1)
        l0 = np.floor(lod).astype(int)
        f = lod - l0
        u = x / scale
        v = z / scale
        out = np.zeros_like(u)
        for level in np.unique(l0):
            m = l0 == level
            a = sample(levels[level], u[m], v[m])
            b = sample(levels[min(level + 1, len(levels) - 1)], u[m], v[m])
            out[m] = a + (b - a) * f[m]
        return out


# ---------------------------------------------------------------------------
# CloudRaymarch.hlsli と同じ式
# ---------------------------------------------------------------------------

class Clouds:
    def __init__(self, p, noise, shaft_points=None):
        self.p = p
        self.noise = noise
        self.shaft = None
        if shaft_points is not None and len(shaft_points) >= 2:
            pts = np.asarray(shaft_points, float)
            pad = p["shaftRadius"] + p["shaftWallDepth"] * 1.5
            self.shaft = (pts[:-1], pts[1:] - pts[:-1], pts.min(0) - pad, pts.max(0) + pad)

    # --- 形 ---------------------------------------------------------------

    def cloud_noise(self, x, z, dist):
        p = self.p
        fade = self.detail_fade(dist)
        large = self.noise.sample("shape", x, z, p["shapeScale"], dist, fade * p["detailFadeLod"])
        if p["detailWeight"] > 0:
            # 同じノイズを小さいスケールでも引いて混ぜる（煙感）。混ぜて狭まった分布を広げ直して補正値の意味を保つ
            small = self.noise.sample("shape", x + 517.0, z + 263.0, p["detailScale"], dist)
            w = p["detailWeight"]
            large = np.clip(0.5 + ((1 - w) * large + w * small - 0.5) / np.sqrt((1 - w) ** 2 + w * w), 0, 1)
        if p["mokoHeight"] > 0:
            # もこもこは高さのでこぼこ（moko_offset）として別に足す。ノイズには混ぜない
            return p["noiseFloor"] + (1 - p["noiseFloor"]) * large
        moko = self.noise.sample("moko", x, z, p["mokoScale"], dist, p["mokoLodBias"])
        # もこもこは大きな形の高い所（頂上）だけに乗せる。谷では高さの傾きが 1/ノイズ² 倍に増幅され、細かい段が縞になるため
        w = p["mokoWeight"] * self.moko_mask(large) * (1 - fade)
        n = large + (moko - large) * w
        return p["noiseFloor"] + (1 - p["noiseFloor"]) * n

    def detail_fade(self, dist):
        """遠くの細部の薄め具合（シェーダの DetailFade）"""
        p = self.p
        # 本当の距離で（ミップ用の dist は浅い角度で ÷|dir.y| 水増しされている）。fade_dist が無ければ dist
        d = getattr(self, "fade_dist", None)
        d = dist if d is None or np.shape(d) != np.shape(dist) else d
        x = np.clip((d - p["detailFadeStart"]) / max(p["detailFadeEnd"] - p["detailFadeStart"], 1), 0, 1)
        return x * x * (3 - 2 * x)

    def moko_mask(self, large):
        p = self.p
        x = np.clip((large - p["mokoValleyLow"]) / max(p["mokoValleyHigh"] - p["mokoValleyLow"], 1e-3), 0, 1)
        return x * x * (3 - 2 * x)

    def moko_offset(self, x, z, dist):
        """もこもこの高さのでこぼこ [m]（谷でも増幅されない一定の高さ）"""
        p = self.p
        if p["mokoHeight"] <= 0:
            return np.zeros_like(x)
        moko = self.noise.sample("moko", x, z, p["mokoScale"], dist, p["mokoLodBias"])
        return p["mokoHeight"] * (moko - 0.5)

    def coverage(self, x, z, dist):
        m = self.noise.sample("mask", x, z, self.p["maskScale"], dist)
        return self.p["coverage"] - (m - 0.5) * self.p["maskStrength"]

    def shaft_closest(self, pos):
        """降下の線への最近点と距離（縦穴が無ければ距離は大きな値）"""
        if self.shaft is None:
            return pos, np.full(len(pos), 1e9)
        a, ab, _, _ = self.shaft
        rel = pos[:, None, :] - a[None]
        t = np.clip((rel * ab[None]).sum(-1) / np.maximum((ab * ab).sum(-1), 1e-3)[None], 0, 1)
        c = a[None] + ab[None] * t[..., None]
        d = np.linalg.norm(pos[:, None, :] - c, axis=-1)
        i = d.argmin(1)
        rows = np.arange(len(pos))
        return c[rows, i], d[rows, i]

    def hole_region(self, pos):
        """縦穴のまわり（穴の雲海の式を評価する範囲）。戻り値 (範囲内か, 最近点, 距離)"""
        p = self.p
        if self.shaft is None:
            return np.zeros(len(pos), bool), pos, np.full(len(pos), 1e9)
        _, _, lo, hi = self.shaft
        inside = np.all((pos >= lo) & (pos <= hi), axis=1)
        c, d = self.shaft_closest(pos)
        inside &= d < p["shaftRadius"] + p["shaftWallDepth"] * 1.5
        return inside, c, d

    def hole_over(self, pos, dist, c, d):
        """縦穴の外側の雲（ゼノブレ式を壁・天井・床にも使う。資料の「雲海に穴を空けて」「雲海を縦にも」）。
        壁：中心線から外へ離れるほど濃い「横向きの雲海」。ノイズは壁に沿った座標で引く（XY 面と ZY 面を法線で混ぜる＝継ぎ目なし）
        天井：その場所の上面から capTop 下。上へ行くほど濃い「下向きの雲海」（上から穴が見えない）
        床：下面から capBottom 上。下へ行くほど濃い「上向きの雲海」"""
        p = self.p
        sharp = p["sharpness"]
        cov = p["coverage"]
        # 壁
        to_out = pos - c
        horiz = np.abs(to_out[:, [0, 2]]) / np.maximum(np.abs(to_out[:, [0, 2]]).sum(1, keepdims=True), 1e-3)
        # 穴の半径（数十〜百 m）に対して雲海のノイズは大きすぎるので、座標を shaftNoiseScale 倍に詰めて細かくする
        k = 1.0 / p["shaftNoiseScale"]
        n_zy = self.cloud_noise(pos[:, 2] * k, pos[:, 1] * k, dist * k)
        n_xy = self.cloud_noise(pos[:, 0] * k, pos[:, 1] * k, dist * k)
        n_wall = n_zy * horiz[:, 0] + n_xy * horiz[:, 1]
        dens_wall = np.clip((d - p["shaftRadius"]) / p["shaftWallDepth"], 0, 1)
        over_wall = (n_wall * dens_wall - cov) * sharp
        # 天井・床（ノイズは場所をずらして上面と別の模様に）
        ceil = self.surface_height(pos[:, 0], pos[:, 2], dist) - p["capTop"]
        n_ceil = self.cloud_noise(pos[:, 0] * k + 777.0, pos[:, 2] * k + 333.0, dist * k)
        over_ceil = (n_ceil * np.clip((pos[:, 1] - ceil) / p["capDepth"], 0, 1) - cov) * sharp
        floor = p["bottom"] + p["capBottom"]
        n_floor = self.cloud_noise(pos[:, 0] * k - 555.0, pos[:, 2] * k + 999.0, dist * k)
        over_floor = (n_floor * np.clip((floor + p["floorDepth"] - pos[:, 1]) / p["floorDepth"], 0, 1) - cov) * sharp
        return np.maximum(np.maximum(over_wall, over_ceil), over_floor)

    def over(self, pos, dist):
        p = self.p
        if p["linearHeight"] > 0:
            # 上面からの深さで濃さ（15m で 1）。下面は丸める
            h, _ = self.linear_height(pos[:, 0], pos[:, 2], dist)
            sea = (h - pos[:, 1]) / 15.0 * np.clip((pos[:, 1] - p["bottom"]) / p["bottomFade"], 0, 1)
            inside, c, d = self.hole_region(pos)
            if inside.any():
                sea[inside] = np.minimum(sea[inside], self.hole_over(pos[inside], dist[inside], c[inside], d[inside]))
            return sea
        n = self.cloud_noise(pos[:, 0], pos[:, 2], dist)
        # もこもこの分だけ上面を持ち上げる＝その分だけ低い高さとして濃さを測る
        y = pos[:, 1] - self.moko_offset(pos[:, 0], pos[:, 2], dist)
        density = np.clip((p["top"] - y) / p["seaDepth"], 0, 1) * np.clip((pos[:, 1] - p["bottom"]) / p["bottomFade"], 0, 1)
        sea = (n * density - self.coverage(pos[:, 0], pos[:, 2], dist)) * p["sharpness"]
        inside, c, d = self.hole_region(pos)
        if inside.any():
            # 穴：雲海の中で、穴の外側の雲（壁・天井・床）でも無い所
            sea[inside] = np.minimum(sea[inside], self.hole_over(pos[inside], dist[inside], c[inside], d[inside]))
        return sea

    def linear_height(self, x, z, dist):
        """線形の高さ（linearHeight=1）：頂上〜谷底の範囲はゼノブレ式と同じで、ノイズに比例。谷で傾きが増幅されない"""
        p = self.p
        n = self.cloud_noise(x, z, dist)
        cov = self.coverage(x, z, dist)
        dmin = p["seaDepth"] * cov
        dmax = p["seaDepth"] * cov / max(p["noiseFloor"], 1e-3)
        mix = (n - p["noiseFloor"]) / (1 - p["noiseFloor"])
        return p["top"] - dmin - (dmax - dmin) * (1 - mix) + self.moko_offset(x, z, dist), dmax - dmin

    def surface_height(self, x, z, dist):
        """上面の高さ H = 最高点 − 深さ × 補正値 / ノイズ（ノイズ × 濃さ = 補正値 の所）"""
        p = self.p
        if p["linearHeight"] > 0:
            return self.linear_height(x, z, dist)[0]
        n = np.maximum(self.cloud_noise(x, z, dist), 1e-3)
        depth = np.minimum(p["seaDepth"] * self.coverage(x, z, dist) / n, p["seaDepth"])
        return p["top"] - depth + self.moko_offset(x, z, dist)

    # --- B'（ボリューム） ----------------------------------------------------

    def volume_density(self, pos, dist):
        """濃さ 0..1。中身も含めて積み上げ、3D ノイズで濃淡と隙間を作る（VolumeDensity）"""
        p = self.p
        base = np.clip(self.over(pos, dist), 0, 1)
        q = pos / p["erosionScale"]
        erosion = gradient_noise3d(q) * 0.65 + gradient_noise3d(q * 2.7) * 0.35
        # 勾配ノイズの値は 0.41〜0.59（10〜90%）に固まるので 0..1 に広げ直す（シェーダの kErosionContrast と同じ）
        erosion = np.clip((erosion - 0.5) * 4.6 + 0.5, 0, 1)
        # 3D ノイズが erosion を超える所だけを塊にする（塊と隙間がはっきり分かれる＝もわもわ）
        return base * np.clip((erosion - p["erosion"]) * p["erosionSharp"], 0, 1)

    def march_volume(self, eye, dirs, t0, t1, active):
        """手前を積み上げる（MarchVolume）。戻り値 (乗算済みの色, 透明度)"""
        p = self.p
        sun = SUN_DIRECTION / np.linalg.norm(SUN_DIRECTION)
        steps = int(p["volumeSteps"])
        dt = np.maximum(t1 - t0, 0) / steps
        cos_sun = dirs @ sun
        g = p["rimG"]
        hg = (1 - g * g) / np.maximum(1 + g * g - 2 * g * cos_sun, 1e-4) ** 1.5
        phase = 1 + (hg - 1) * 0.5
        lit_c = np.array(p["litColor"])
        shadow_c = np.array(p["shadowColor"])
        trans = np.ones(len(dirs))
        color = np.zeros((len(dirs), 3))
        live = active & (dt > 0)
        for i in range(steps):
            if not live.any():
                break
            idx = np.nonzero(live)[0]
            t = t0[idx] + (i + 0.5) * dt[idx]
            pos = eye + dirs[idx] * t[:, None]
            dens = self.volume_density(pos, t)
            opt = np.zeros(len(idx))
            for s in (1, 2):
                opt += self.volume_density(pos + sun * p["volumeLightStep"] * s, t) * p["volumeLightStep"]
            sun_t = np.exp(-opt * p["extinction"] * p["lightExtinction"])
            powder = 1 - np.exp(-dens * p["powder"])
            height = np.clip((pos[:, 1] - p["bottom"]) / max(p["top"] - p["bottom"], 1), 0, 1)[:, None]
            lit = lit_c * (sun_t * powder * phase[idx])[:, None] + (shadow_c + (lit_c - shadow_c) * height) * p["ambientStrength"]
            absorb = 1 - np.exp(-dens * p["extinction"] * dt[idx])
            color[idx] += (trans[idx] * absorb)[:, None] * lit
            trans[idx] *= 1 - absorb
            live[idx[trans[idx] < 0.01]] = False
        return color, 1 - trans

    def volume_weight(self, eye):
        """B' の重み（CloudRaymarcher::ComputeVolumeWeight）：入口・出口の近く、または縦穴の中で 1"""
        p = self.p
        if self.shaft is None:
            return 0.0
        pts = np.vstack([self.shaft[0], self.shaft[0][-1:] + self.shaft[1][-1:]])

        def cross(y):
            for a, b in zip(pts[:-1], pts[1:]):
                if (a[1] - y) * (b[1] - y) <= 0 and a[1] != b[1]:
                    return a + (b - a) * (y - a[1]) / (b[1] - a[1])
            return None

        def zone(center, inner, outer):
            if center is None:
                return 0.0
            x = np.clip((np.linalg.norm(eye - center) - inner) / max(outer - inner, 1), 0, 1)
            return 1 - x * x * (3 - 2 * x)

        w = max(zone(cross(p["top"]), p["volumeZoneInner"], p["volumeZoneOuter"]),
                zone(cross(p["bottom"]), p["exitZoneInner"], p["exitZoneOuter"]))
        _, d = self.shaft_closest(eye[None])
        if p["bottom"] <= eye[1] <= p["top"]:
            x = np.clip((d[0] - p["shaftRadius"]) / 50.0, 0, 1)
            w = max(w, (1 - x * x * (3 - 2 * x)) * p["volumeShaftWeight"])
        return float(w)

    # --- 歩き方 -----------------------------------------------------------

    def trace(self, eye, dirs, t0, t_end, active):
        """上面の高さ場トレース（TraceSurface）。戻り値 (t, hit)
        面との高さの差 /（下向きの傾き＋傾斜の見積もり）だけ進み、面の下に入ったら直前の点との間を 2 分探索して面の上に戻す。
        2 分探索なしで「面に近づいたら止める」だと、浅い角度では歩数を使い切った所や面の少し上で止まり、
        止まる位置が歩数の切り替わりで段々に変わって、指紋のような縞（陰影・谷の暗さの両方）になった"""
        p = self.p
        descent = -np.minimum(dirs[:, 1], -1e-4) + p["traceSlope"]
        grazing = np.maximum(np.abs(dirs[:, 1]), p["grazingMin"]) ** p["grazingPower"]
        t = t0.copy()
        prev = t0.copy()
        active = active & (t < t_end)
        hit = np.zeros(len(t), bool)
        for _ in range(int(p["steps"])):
            if not active.any():
                break
            pos = eye + dirs * t[:, None]
            self.fade_dist = t
            gap = pos[:, 1] - self.surface_height(pos[:, 0], pos[:, 2], t / grazing)
            self.fade_dist = None
            done = active & (gap < p["traceEpsilon"])
            hit |= done
            active &= ~done
            prev = np.where(active, t, prev)
            t = np.where(active, t + np.maximum(gap, 0) / descent, t)
            active &= t < t_end
        # 当たった所を面まで詰める：少し先（面の下）を探して挟み、2 分探索。挟めなければそのまま。
        # 「面から traceEpsilon 以内で止める」だけだと、浅い角度では水平に最大 traceEpsilon/|dir.y| 手前で止まり、
        # その位置が歩数で段々に変わって、陰影に指紋のような縞が出た
        h = np.nonzero(hit)[0]
        if len(h) and p["traceRefine"] > 0:
            lo = t[h].copy()
            reach = 2.0 * p["traceEpsilon"] / np.maximum(-dirs[h, 1], p["grazingMin"])
            hi = lo + reach
            qh = eye + dirs[h] * hi[:, None]
            under = qh[:, 1] < self.surface_height(qh[:, 0], qh[:, 2], hi / grazing[h])
            for _ in range(int(p["traceRefine"])):
                mid = 0.5 * (lo + hi)
                q = eye + dirs[h] * mid[:, None]
                u = q[:, 1] < self.surface_height(q[:, 0], q[:, 2], mid / grazing[h])
                hi = np.where(u, mid, hi)
                lo = np.where(u, lo, mid)
            t[h] = np.where(under, hi, t[h])
        return np.minimum(t, t_end), hit | active

    def march_geometric(self, eye, dirs, t0, t_end, active):
        """等比で歩いて over > 0 の最初の点（FindHitGeometric）。交点は線形補間"""
        p = self.p
        steps = int(p["steps"])
        span = np.maximum(t_end - t0, 0)
        growth = np.log2(1 + span / p["firstStep"])
        prev_t = t0.copy()
        prev_o = self.over(eye + dirs * t0[:, None], t0)
        hit = active & (prev_o > 0)
        t = t0.copy()
        live = active & ~hit & (span > 0)
        for i in range(1, steps + 1):
            if not live.any():
                break
            ti = t0 + p["firstStep"] * (np.exp2(growth * (i - 0.5) / steps) - 1)
            o = np.full(len(t0), -1.0)
            o[live] = self.over(eye + dirs[live] * ti[live, None], ti[live])
            new = live & (o > 0)
            f = np.clip(prev_o / np.minimum(prev_o - o, -1e-6), 0, 1)
            t[new] = (prev_t + (ti - prev_t) * f)[new]
            hit |= new
            live &= ~new
            prev_t = np.where(live, ti, prev_t)
            prev_o = np.where(live, o, prev_o)
        return t, hit

    # --- ゼノブレ式（上から見下ろす雲海） ------------------------------------------

    def sun_direction(self):
        """太陽へ向かう向き。sunElevation [度] > 0 なら、ステージの太陽の方角のまま仰角だけ変える"""
        sun = SUN_DIRECTION / np.linalg.norm(SUN_DIRECTION)
        if self.p["sunElevation"] > 0:
            h = np.array([sun[0], 0.0, sun[2]])
            h /= np.linalg.norm(h)
            e = np.radians(self.p["sunElevation"])
            sun = np.array([h[0] * np.cos(e), np.sin(e), h[2] * np.cos(e)])
        return sun

    def xeno_alpha(self, over, dist):
        """透明度 = saturate(over)。遠いほど乗算値を下げて柔らかく（遠景の歩幅の粗さを溶かす）"""
        return np.clip(over / (1.0 + dist / self.p["softDistance"]), 0, 1)

    def xeno_light(self, pos, dist):
        """ゼノブレ式の陰影：太陽の方へ進んだ点の透明度で、明るい色と暗い色を混ぜる（lightTaps 点）"""
        p = self.p
        sun = self.sun_direction()
        occ = np.zeros(len(pos))
        for k in range(1, int(p["lightTaps"]) + 1):
            occ += np.clip(self.over(pos + sun * (p["lightStep"] * k), dist), 0, 1)
        return np.exp(-occ * p["density"])

    def xeno_color(self, pos, dist):
        p = self.p
        lit = np.array(p["litColor"])
        shadow = np.array(p["shadowColor"])
        k = np.clip(self.xeno_light(pos, dist) * (1 - p["ambient"]) + p["ambient"], 0, 1)
        return shadow + (lit - shadow) * k[:, None]

    def xeno_render(self, eye, dirs, t0, t_end, valid):
        """最高点〜帯の底（最高点 − 深さ）を等間隔に歩き、透明度が 0 を超えた点から手前→奥へ重ねる。
        帯の底より下は詰まった雲なので、底まで来たら残りを不透明にする。戻り値 (乗算済みの色, 透明度, 最初に当たった距離)"""
        p = self.p
        count = len(dirs)
        dy = np.minimum(dirs[:, 1], -1e-4)
        t_band = (p["top"] - p["seaDepth"] - eye[1]) / dy
        t1 = np.minimum(t_band, t_end)
        steps = int(p["steps"])
        dt = np.maximum(t1 - t0, 0) / steps
        color = np.zeros((count, 3))
        trans = np.ones(count)
        first = np.full(count, np.nan)
        live = valid & (dirs[:, 1] < -1e-4) & (dt > 0)
        for i in range(steps):
            if not live.any():
                break
            idx = np.nonzero(live)[0]
            t = t0[idx] + (i + 0.5) * dt[idx]
            pos = eye + dirs[idx] * t[:, None]
            a = self.xeno_alpha(self.over(pos, t), t)
            if p["accumulate"] > 0:
                # 1 歩の不透明度を歩幅に合わせる（refStep [m] 進んで a になる）。歩数を変えても見た目が変わらない
                a = 1 - (1 - np.minimum(a, 0.999)) ** (dt[idx] / p["refStep"])
            if p["accumulate"] <= 0:
                # 純粋なゼノブレ式：最初に 0 を超えた点の透明度をそのまま使って打ち切る
                a = np.where(a > 0, a, 0)
                stop = a > 0
            else:
                stop = np.zeros(len(idx), bool)
            hit_now = a > 0
            if hit_now.any():
                h = idx[hit_now]
                first[h] = np.where(np.isnan(first[h]), t[hit_now], first[h])
                c = self.xeno_color(pos[hit_now], t[hit_now])
                color[h] += (trans[h] * a[hit_now])[:, None] * c
                trans[h] *= 1 - a[hit_now]
            done = stop | (trans[idx] < 0.01)
            live[idx[done]] = False
        # 帯の底まで来た：その下は詰まった雲。底の点の色で残りを埋める
        bottom = valid & (dirs[:, 1] < -1e-4) & (t_band < t_end) & (trans > 0.01)
        if p["accumulate"] <= 0:
            bottom &= np.isnan(first)
        if bottom.any():
            pos = eye + dirs[bottom] * t_band[bottom, None]
            c = self.xeno_color(pos, t_band[bottom])
            color[bottom] += trans[bottom, None] * c
            first[bottom] = np.where(np.isnan(first[bottom]), t_band[bottom], first[bottom])
            trans[bottom] = 0
        return color, 1 - trans, np.nan_to_num(first, nan=0.0)

    def render(self, eye, dirs, scene_distance):
        """MarchSurface と同じ分岐。戻り値は (色, 透明度)"""
        p = self.p
        count = len(dirs)
        dy = np.where(np.abs(dirs[:, 1]) < 1e-5, 1e-5, dirs[:, 1])
        ta = (p["top"] - eye[1]) / dy
        tb = (p["bottom"] - eye[1]) / dy
        t_enter = np.maximum(np.minimum(ta, tb), 0)
        t_exit = np.maximum(ta, tb)
        if p["bottom"] <= eye[1] <= p["top"]:
            t_enter[:] = 0
        t_end = np.minimum(np.minimum(t_exit, scene_distance), p["maxDistance"])
        valid = t_end > t_enter

        # B'：手前 volumeRange × 重み を積み上げ、その先をゼノブレ式で続ける
        w = self.volume_weight(eye) if p["volume"] > 0 else 0.0
        vol_color = np.zeros((count, 3))
        vol_alpha = np.zeros(count)
        if w > 0:
            t_vol = np.minimum(t_enter + p["volumeRange"] * w, t_end)
            vol_color, vol_alpha = self.march_volume(eye, dirs, t_enter, t_vol, valid)
            t_enter = np.where(valid, t_vol, t_enter)
            print(f"B' weight {w:.2f}")

        if eye[1] > p["top"] and p["hv"] > 0:
            hp = dict(p)
            col, alpha = HeightVolume(hp, self.noise).render(eye, dirs, t_end)
            return col, alpha

        if eye[1] > p["top"] and p["xeno"] > 0:
            pre, alpha, first = self.xeno_render(eye, dirs, t_enter, t_end, valid)
            count = len(dirs)
            horizon = sky(np.full(count, 0.02))
            # 空気遠近：重ねた色を、最初に当たった距離で水平線の色へ寄せる
            haze = np.clip((first - p["hazeStart"]) / max(p["hazeEnd"] - p["hazeStart"], 1), 0, 1) * p["hazeMax"]
            fade = np.clip((first - p["fadeStart"]) / max(p["fadeEnd"] - p["fadeStart"], 1), 0, 1)
            k = np.maximum(haze, fade)[:, None]
            pre = pre * (1 - k) + horizon * alpha[:, None] * k
            miss = valid & (alpha <= 0) & (dirs[:, 1] < 0) & (t_end >= p["maxDistance"] - 1)
            pre[miss] = horizon[miss]
            alpha[miss] = 1.0
            out_color = vol_color + (1 - vol_alpha)[:, None] * pre
            out_alpha = vol_alpha + (1 - vol_alpha) * alpha
            return out_color / np.maximum(out_alpha, 1e-4)[:, None], out_alpha

        if eye[1] > p["top"]:
            t, hit = self.trace(eye, dirs, t_enter, t_end, valid)
            pos = eye + dirs * t[:, None]
            in_hole = hit & (self.over(pos, t) <= 0)
            if in_hole.any():
                t2, hit2 = self.march_geometric(eye, dirs, t, t_end, in_hole)
                t = np.where(in_hole, t2, t)
                hit = np.where(in_hole, hit2, hit)
        else:
            t, hit = self.march_geometric(eye, dirs, t_enter, t_end, valid)
        color, alpha = self.shade(eye, dirs, t, hit, t_end)
        # 上から見下ろして物に遮られず最大距離まで当たらなかった＝水平線のすぐ下
        if eye[1] > p["top"]:
            miss = valid & ~hit & (t_end >= p["maxDistance"] - 1)
            horizon = sky(np.full(count, 0.02))
            color[miss] = horizon[miss]
            alpha[miss] = 1.0
        # 前（B'）と後ろ（面）を重ねる。B' の色は乗算済み
        out_color = vol_color + (1 - vol_alpha)[:, None] * color * alpha[:, None]
        out_alpha = vol_alpha + (1 - vol_alpha) * alpha
        return out_color / np.maximum(out_alpha, 1e-4)[:, None], out_alpha

    # --- 陰影 -------------------------------------------------------------

    def surface_frame(self, pos, dist):
        """面の法線・雲の中へ向かう向き・暗さ（SurfaceFrame）。上面は高さ場、縦穴の壁は中心線へ向く、底面は下向き"""
        p = self.p
        # 上面の法線：焼いた傾きから（高さを差分すると、バイリニアの折れ目と量子化の段差で等高線のような縞が出た）
        n0 = np.maximum(self.cloud_noise(pos[:, 0], pos[:, 2], dist), 1e-3)
        cov = self.coverage(pos[:, 0], pos[:, 2], dist)
        # 大きな形は高さの傾き（1/ノイズ² で増幅）、もこもこは増幅しない一定の強さの凹凸（谷で細かい段が縞にならないように）
        fade = self.detail_fade(dist)
        w = p["mokoWeight"] * (1 - fade)
        sx = self.noise.sample("shape_du", pos[:, 0], pos[:, 2], p["shapeScale"], dist, fade * p["detailFadeLod"]) / p["shapeScale"]
        sz = self.noise.sample("shape_dv", pos[:, 0], pos[:, 2], p["shapeScale"], dist, fade * p["detailFadeLod"]) / p["shapeScale"]
        mx = self.noise.sample("moko_du", pos[:, 0], pos[:, 2], p["mokoScale"], dist, p["mokoLodBias"]) / p["mokoScale"]
        mz = self.noise.sample("moko_dv", pos[:, 0], pos[:, 2], p["mokoScale"], dist, p["mokoLodBias"]) / p["mokoScale"]
        k = p["seaDepth"] * cov / (n0 * n0) * (1 - p["noiseFloor"])
        k_ref = p["seaDepth"] * cov / (MOKO_NOISE_REF * MOKO_NOISE_REF) * (1 - p["noiseFloor"]) * p["mokoNormal"]
        if p["linearHeight"] > 0:
            # 線形の高さの傾き：(dmax − dmin) × 混ぜたノイズの傾き（一定倍）
            dmin = p["seaDepth"] * cov
            dmax = p["seaDepth"] * cov / max(p["noiseFloor"], 1e-3)
            gx = (dmax - dmin) * ((1 - w) * sx + w * mx)
            gz = (dmax - dmin) * ((1 - w) * sz + w * mz)
        elif p["mokoHeight"] > 0:
            # 形と同じ式：大きな形の傾き（増幅あり）＋もこもこの高さの傾き（一定）
            gx = k * sx + p["mokoHeight"] * mx
            gz = k * sz + p["mokoHeight"] * mz
        elif p["mokoValleyHigh"] > 0:
            large = self.noise.sample("shape", pos[:, 0], pos[:, 2], p["shapeScale"], dist)
            we = w * self.moko_mask(large)
            gx = k * ((1 - we) * sx + we * mx)
            gz = k * ((1 - we) * sz + we * mz)
        else:
            gx = k * (1 - w) * sx + k_ref * w * mx
            gz = k * (1 - w) * sz + k_ref * w * mz
        normal = np.stack([-gx, np.ones_like(n0), -gz], -1)
        normal /= np.linalg.norm(normal, axis=-1, keepdims=True)

        # 上面の谷：こぶの高さの範囲の下ほど暗い
        depth_min = p["seaDepth"] * cov
        depth_max = p["seaDepth"] * cov / max(p["noiseFloor"], 1e-3)
        # 止まった点の高さではなく、その場所の上面の高さで決める（遠くはレイが面の少し上で止まり、
        # 止まる高さが歩数の切り替わりで段々に変わって、等高線のような縞が出た）
        surface_y = self.surface_height(pos[:, 0], pos[:, 2], dist)
        valley = np.clip((p["top"] - surface_y - depth_min) / np.maximum(depth_max - depth_min, 1), 0, 1)
        ao = 1 - p["valley"] * valley ** p["valleyPow"]

        # 縦穴のまわり：濃さの式の傾き（中心差分）から法線。深いほど空が見えず暗い
        inside, c, d = self.hole_region(pos)
        surf = self.surface_height(pos[:, 0], pos[:, 2], dist)
        below_ceiling = np.clip((surf - p["capTop"] * 0.5 - pos[:, 1]) / max(p["capTop"] * 0.5, 1), 0, 1)
        w = np.where(inside, np.clip((p["shaftRadius"] + p["shaftWallDepth"] * 1.5 - d) / p["shaftWallBlend"], 0, 1), 0.0) * below_ceiling
        if inside.any():
            q = pos[inside]
            qd = dist[inside]
            h = p["normalEps"] * 0.5
            grad = np.zeros_like(q)
            for axis in range(3):
                off = np.zeros(3)
                off[axis] = h
                grad[:, axis] = self.over(q + off, qd) - self.over(q - off, qd)
            g = -grad / np.maximum(np.linalg.norm(grad, axis=-1, keepdims=True), 1e-6)
            normal[inside] = normal[inside] * (1 - w[inside, None]) + g * w[inside, None]
        ceiling = surf - p["capTop"]
        hole_depth = np.clip((ceiling - pos[:, 1]) / np.maximum(ceiling - p["bottom"] - p["capBottom"], 1), 0, 1)
        ao = ao * (1 - w) + (1 - p["shaftDarkness"] * np.sqrt(hole_depth)) * w

        # 底面：下向き。地面の照り返しだけの暗さ
        u = 1 - np.clip((pos[:, 1] - p["bottom"]) / p["bottomFade"], 0, 1)
        normal = normal * (1 - u[:, None]) + np.array([0.0, -1.0, 0.0]) * u[:, None]
        ao = ao * (1 - u) + p["undersideBright"] * u

        normal /= np.maximum(np.linalg.norm(normal, axis=-1, keepdims=True), 1e-3)
        return normal, ao, w

    def shade(self, eye, dirs, t, hit, t_end):
        p = self.p
        count = len(dirs)
        sun = SUN_DIRECTION / np.linalg.norm(SUN_DIRECTION)
        pos = eye + dirs * t[:, None]
        # 陰影のミップは見る角度でも粗くする（浅い角度では 1 画素が奥行き方向に長い範囲を覆い、細かい凹凸がモアレになる）
        shade_dist = t / np.maximum(np.abs(dirs[:, 1]), p["grazingMin"]) ** p["grazingPower"]
        self.fade_dist = t
        normal, ao, wall = self.surface_frame(pos, shade_dist)

        # 面に当たったら不透明（雲海の面の奥は中身の詰まった雲）。奥を測って半透明にすると、ふたを突き抜けて穴が透けた
        alpha = np.where(hit, 1.0, 0.0)

        ndl = np.clip((normal @ sun) * (1 - p["wrap"]) + p["wrap"], 0, 1)
        occlusion = np.clip(self.over(pos + sun * p["lightStep"], t), 0, 1)
        sunlit = ndl * np.exp(-occlusion * p["density"])
        light = np.clip((sunlit * (1 - p["ambient"]) + p["ambient"]) * ao, 0, 1)

        lit = np.array(p["litColor"])
        shadow = np.array(p["shadowColor"])
        color = shadow + (lit - shadow) * light[:, None]
        haze = np.clip((t - p["hazeStart"]) / max(p["hazeEnd"] - p["hazeStart"], 1), 0, 1) * p["hazeMax"]
        horizon = sky(np.full(count, 0.02))  # 寄せる先は水平線の空の色（シェーダと同じ）
        fade = np.clip((t - p["fadeStart"]) / max(p["fadeEnd"] - p["fadeStart"], 1), 0, 1)
        color = color + (horizon - color) * np.maximum(haze, fade)[:, None]
        return color, alpha


class HeightVolume:
    """ハイトマップ誘導のボリューム雲（試作）。
    ① 上面 T(x,z) を高さの地形としてトレースし、② 上面の下の「綿の層」だけを数歩積み上げる。
    影は太陽の方へハイトマップを読んで判定、谷の暗さはその場所の高さとぼかした高さの比較。法線は使わない"""

    def __init__(self, p, noise):
        self.p = p
        self.noise = noise

    # --- 形 ---------------------------------------------------------------

    def height_noise(self, x, z, dist, bias=0.0):
        """0..1。大きな形と小さな形（同じノイズを別スケール）を混ぜる"""
        p = self.p
        large = self.noise.sample("shape", x, z, p["shapeScale"], dist, bias)
        small = self.noise.sample("shape", x + 517.0, z + 263.0, p["detailScale"], dist, bias)
        w = p["detailWeight"]
        n = (1 - w) * large + w * small
        return np.clip(0.5 + (n - 0.5) / np.sqrt((1 - w) ** 2 + w * w), 0, 1)

    def top(self, x, z, dist, bias=0.0):
        """上面の高さ。ノイズ 1 で最高点、補正値より下は谷（relief [m] だけ沈む）"""
        p = self.p
        n = self.height_noise(x, z, dist, bias)
        # 補正値（coverage）を境に上へ盛り上がる。smoothstep で山の裾をなだらかに
        # 補正値より下（谷の床）も平らにしない：floorSpread だけ下までなだらかに続ける
        low = p["coverage"] - p["floorSpread"]
        x01 = np.clip((n - low) / max(1 - low, 1e-3), 0, 1)
        bump = x01 * x01 * (3 - 2 * x01)
        return p["top"] - p["relief"] * (1 - bump ** p["bumpPower"])

    def density(self, pos, dist):
        """濃さ 0..1：上面からの深さを綿の層の厚さで割り、3D ノイズで崩す（縁が溶ける）"""
        p = self.p
        x, z = pos[:, 0], pos[:, 2]
        if p["warp"] > 0:
            # ハイトマップを読む位置を 3D ノイズで高さごとにずらす。急な側面で横の模様が縦に伸びる（高さ場の宿命）のを崩す。
            # 上面の近くはずらさない（ずらすと上面の上に小さな切れ端が浮いた）。深いほど強く
            ramp = np.clip((self.top(x, z, dist) - pos[:, 1]) / p["warpRamp"], 0, 1)
            w = pos / p["warpScale"]
            x = x + (gradient_noise3d(w) - 0.5) * 2 * p["warp"] * ramp
            z = z + (gradient_noise3d(w + 31.7) - 0.5) * 2 * p["warp"] * ramp
        t = self.top(x, z, dist)
        depth = (t - pos[:, 1]) / (p["fluff"] + dist * p["fluffPerMeter"])
        q = pos / p["erosionScale"]
        n3 = gradient_noise3d(q) * 0.65 + gradient_noise3d(q * 2.7) * 0.35
        n3 = np.clip((n3 - 0.5) * 4.6 + 0.5, 0, 1)
        return np.clip(depth + (n3 - 0.5) * p["erosion"], 0, 1)

    # --- 光 ---------------------------------------------------------------

    def light(self, pos, dist, dens):
        p = self.p
        sun = Clouds.sun_direction(self)  # 同じ仰角の上書きを使う
        t_here = self.top(pos[:, 0], pos[:, 2], dist)
        depth = np.maximum(t_here - pos[:, 1], 0)
        # 太陽の方へ数点、濃さを積んで減衰を出す（光側マーチ）。自分の塊の厚みも、他の塊が落とす影もこれで入る。
        # 真上の上面からの深さで減衰させると、塊の側面が縦の筋になった
        od = np.zeros(len(pos))
        prev = 0.0
        for s in p["lightSteps"]:
            od += self.density(pos + sun * s, dist) * (s - prev)
            prev = s
        sun_vis = np.exp(-p["lightSigma"] * od)
        # powder：薄い縁は明るく、中は暗く（綿の光り方）
        powder = 1 - np.exp(-2.0 * dens)
        sun_term = sun_vis * ((1 - p["powderMix"]) + p["powderMix"] * powder)
        # 谷の暗さ：その場所の上面が、まわり（ぼかしたミップ）より低いほど空が見えず暗い
        t_avg = self.top(pos[:, 0], pos[:, 2], dist, p["aoBias"])
        # 高さは上面より上に出さない（上面の上に 3D ノイズで出たもやが、高いぶん明るくなって白い筋・切れ端に見えた）
        ao = np.clip(0.5 + (np.minimum(pos[:, 1], t_here) - t_avg) / p["aoRange"], 0, 1)
        ao *= np.exp(-depth / p["aoDepth"])  # 深い所ほど暗い
        lit = np.array(p["litColor"])
        amb_top = np.array(p["skyAmbient"])
        amb_low = np.array(p["shadowColor"])
        ambient = amb_low + (amb_top - amb_low) * ao[:, None]
        return lit * (sun_term * p["sunScale"])[:, None] + ambient * p["ambientScale"]

    # --- 描画 -------------------------------------------------------------

    def trace(self, eye, dirs, t0, t_end, active):
        """上面に当たるまで：面との高さの差 /（下向きの傾き＋最大傾斜）だけ進む"""
        p = self.p
        descent = -np.minimum(dirs[:, 1], -1e-4) + p["traceSlope"]
        t = t0.copy()
        active = active & (t < t_end)
        hit = np.zeros(len(t), bool)
        for _ in range(int(p["steps"])):
            if not active.any():
                break
            idx = np.nonzero(active)[0]
            pos = eye + dirs[idx] * t[idx, None]
            gap = pos[:, 1] - self.top(pos[:, 0], pos[:, 2], t[idx])
            done = gap < p["traceEpsilon"]
            hit[idx[done]] = True
            active[idx[done]] = False
            t[idx] = np.where(done, t[idx], t[idx] + np.maximum(gap, 0) / descent[idx])
            active &= t < t_end
        # 傾きの見積もりを超える急な所では面を突き抜けるので、面の上まで戻す（gap が 0 になる所を 2 分探索）
        h = np.nonzero(hit)[0]
        if len(h):
            lo = np.maximum(t[h] - 200.0, t0[h])
            hi = t[h]
            for _ in range(8):
                mid = 0.5 * (lo + hi)
                q = eye + dirs[h] * mid[:, None]
                under = q[:, 1] < self.top(q[:, 0], q[:, 2], mid)
                hi = np.where(under, mid, hi)
                lo = np.where(under, lo, mid)
            t[h] = hi
        return np.minimum(t, t_end), hit | active

    def march_segment(self, eye, dirs, t_start, length, active, color, trans, last_c, has_c):
        """t_start から length だけ綿の層を積み上げる"""
        p = self.p
        n = int(p["volumeSteps"])
        dt = length / n
        live = active.copy()
        for i in range(n):
            if not live.any():
                break
            idx = np.nonzero(live)[0]
            t = t_start[idx] + (i + 0.5) * dt[idx]
            pos = eye + dirs[idx] * t[:, None]
            dens = self.density(pos, t)
            a = 1 - np.exp(-p["extinction"] * dens * dt[idx])
            c = self.light(pos, t, dens)
            inside = dens > 0.05
            last_c[idx[inside]] = c[inside]
            has_c[idx[inside]] = True
            color[idx] += (trans[idx] * a)[:, None] * c
            trans[idx] *= 1 - a
            live[idx[trans[idx] < 0.02]] = False

    def render(self, eye, dirs, t_end_all):
        p = self.p
        count = len(dirs)
        down = dirs[:, 1] < -1e-4
        dy = np.where(down, dirs[:, 1], -1e-4)
        t0 = np.maximum((p["top"] - eye[1]) / dy, 0)
        t_end = np.minimum(t_end_all, p["maxDistance"])
        slope = np.maximum(-dirs[:, 1], p["grazingMin"])
        color = np.zeros((count, 3))
        trans = np.ones(count)
        last_c = np.zeros((count, 3))
        has_c = np.zeros(count, bool)
        first_hit = np.full(count, np.nan)
        ever_hit = np.zeros(count, bool)
        t_cur = t0.copy()
        active = down.copy()
        end_inside = np.zeros(count, bool)
        # 綿の層を抜けて空中に出たレイは、次の上面を探してもう一度積む（最大 segments 回）。
        # 1 回で打ち切って残りを不透明に塗ると、小さな塊の縁や切れ端がパリっとした白い板になった
        for seg in range(int(p["segments"])):
            if not active.any():
                break
            t_hit, hit = self.trace(eye, dirs, t_cur, t_end, active)
            hit &= active
            if not hit.any():
                break
            # 上面の少し上から歩き始める（縁の柔らかい所を拾う）。遠いほど縁を厚く（1 画素より細い縁はパリっと切れて見える）
            soft = p["fluff"] + t_hit * p["fluffPerMeter"]
            t_hit = np.maximum(t_hit - p["edgeLead"] * soft / slope, t_cur)
            first_hit = np.where(hit & np.isnan(first_hit), t_hit, first_hit)
            seg_count = getattr(self, "_seg", np.zeros(count)) if seg else np.zeros(count)
            seg_count[hit] += 1
            self._seg = seg_count
            ever_hit |= hit
            length = np.minimum((p["fluff"] + t_hit * p["fluffPerMeter"]) * (p["marchDepth"] + p["edgeLead"]) / slope, p["maxMarch"])
            self.march_segment(eye, dirs, t_hit, length, hit, color, trans, last_c, has_c)
            t_next = t_hit + length
            q = eye + dirs * t_next[:, None]
            inside_now = q[:, 1] < self.top(q[:, 0], q[:, 2], t_next) - p["fluff"] * 0.5
            end_inside = np.where(hit, inside_now, end_inside)
            # 層の中で終わった（中身の詰まった雲）か、もう透けない所は終わり。空中に出た所は続ける
            active = hit & ~inside_now & (trans > 0.02) & (t_next < t_end)
            t_cur = np.where(active, t_next, t_cur)
        # 中身の詰まった雲の中で終わった分だけを埋める（後ろの青が透けない）
        self.debug = dict(seg=self._seg.copy() if hasattr(self, '_seg') else None, trans_before_fill=trans.copy(), ever_hit=ever_hit.copy(), end_inside=end_inside.copy(), first_hit=first_hit.copy())
        rest = ever_hit & end_inside & (trans > 0)
        if rest.any():
            idx = np.nonzero(rest)[0]
            # 埋める色は「それまで積んだ色の平均」。最後の 1 点の色で埋めると、積みが浅い所だけ明るさが変わって
            # 縁のくっきりした板に見えた
            acc = 1 - trans[idx]
            mean_c = color[idx] / np.maximum(acc, 1e-4)[:, None]
            fallback = np.where(has_c[idx, None], last_c[idx], np.array(p["shadowColor"]))
            w = np.clip(acc / p["fillMeanMin"], 0, 1)[:, None]
            color[idx] += trans[idx, None] * (mean_c * w + fallback * (1 - w))
            trans[idx] = 0
        alpha = 1 - trans
        horizon = sky(np.full(count, 0.02))
        fh = np.nan_to_num(first_hit, nan=p["maxDistance"])
        haze = np.clip((fh - p["hazeStart"]) / max(p["hazeEnd"] - p["hazeStart"], 1), 0, 1) * p["hazeMax"]
        fade = np.clip((fh - p["fadeStart"]) / max(p["fadeEnd"] - p["fadeStart"], 1), 0, 1)
        k = np.maximum(haze, fade)[:, None]
        color = color * (1 - k) + horizon * alpha[:, None] * k
        # 水平線の近くで抜けた所・当たらなかった所は水平線の色で埋める
        rest_h = down & (alpha < 1)
        color[rest_h] += (1 - alpha[rest_h])[:, None] * horizon[rest_h]
        alpha[rest_h] = 1.0
        return color / np.maximum(alpha, 1e-4)[:, None], alpha


HV_DEFAULTS = dict(
    # 2026-10-22 の試作で決めた値。形（floorSpread 1＝平らな床なし：床の縁がくっきりした板に見えた）
    shapeScale=1500.0, detailScale=400.0, detailWeight=0.35, relief=60.0, bumpPower=1.0, floorSpread=1.0,
    warp=20.0, warpScale=50.0, warpMargin=0.0, warpRamp=20.0, segments=3, fluffPerMeter=0.0, edgeLead=0.0, fillMeanMin=0.01,
    # 綿の層
    # （試作中は B' の既定値と名前が重なっていて、実際にはこの値で描いていた：48 歩・消散 0.015・崩し 0.5・3D ノイズ 100m）
    fluff=12.0, marchDepth=2.0, maxMarch=400.0, volumeSteps=48, extinction=0.1, erosion=0.5, erosionScale=100.0,
    # 光（消散 0.1＝どのレイも上面近くで不透明になる。弱いと、斜めに長く歩いた所だけ中の暗い色を拾って板に見えた）
    lightSteps=(6.0, 18.0, 40.0, 90.0), lightSigma=0.4, powderMix=0.6,
    aoBias=3.0, aoRange=60.0, aoDepth=120.0,
    litColor=[1.0, 0.97, 0.92], shadowColor=[0.28, 0.40, 0.70], skyAmbient=[0.50, 0.66, 0.95],
    ambientScale=0.55, sunScale=0.7,
)


def gradient_noise3d(p):
    """3D の勾配ノイズ ≒ 0..1（GradientNoise3D と同じ考え方。ハッシュは整数で作る）"""
    i = np.floor(p).astype(np.int64)
    f = p - i
    u = f * f * (3 - 2 * f)

    def grad(ix, iy, iz):
        # シェーダの Hash33 と同じく、各成分が -1..1 に一様な（正規化しない）勾配
        h = (ix * 73856093) ^ (iy * 19349663) ^ (iz * 83492791)
        h = (h ^ (h >> 13)) * 1274126177
        h2 = (h ^ (h >> 15)) * 2246822519
        return np.stack([((h >> 4) & 0x3FF) / 1023.0 * 2 - 1,
                         ((h >> 20) & 0x3FF) / 1023.0 * 2 - 1,
                         ((h2 >> 12) & 0x3FF) / 1023.0 * 2 - 1], -1)

    n = np.zeros(len(p))
    for dz in (0, 1):
        for dy in (0, 1):
            for dx in (0, 1):
                o = np.array([dx, dy, dz])
                w = (u[:, 0] if dx else 1 - u[:, 0]) * (u[:, 1] if dy else 1 - u[:, 1]) * (u[:, 2] if dz else 1 - u[:, 2])
                gv = grad(i[:, 0] + dx, i[:, 1] + dy, i[:, 2] + dz)
                n += w * (gv * (f - o)).sum(-1)
    return n * 0.5 + 0.5


# ---------------------------------------------------------------------------
# レール（Resources/Json/Scenes/StagePlay.json の CameraPath ＋ Tuning の camera.speed）
# ---------------------------------------------------------------------------

def load_rail():
    scene = json.loads(SCENE_PATH.read_text(encoding="utf-8-sig"))
    path = next(o for o in scene["objects"] if o.get("type") == "Spline" and o.get("name") == "CameraPath")
    speed = json.loads(TUNING_PATH.read_text(encoding="utf-8-sig"))["camera"]["speed"]
    return np.array(path["points"], float), speed


def rail_sample(points, t01):
    """SplineCurveActor::Sample と同じ Catmull-Rom（制御点インデックスで等分）"""
    n = len(points)
    scaled = min(max(t01, 0.0), 1.0) * (n - 1)
    seg = min(int(math.floor(scaled)), n - 2)
    u = scaled - seg
    p0, p1, p2, p3 = (points[min(max(i, 0), n - 1)] for i in (seg - 1, seg, seg + 1, seg + 2))
    return 0.5 * (2 * p1 + (-p0 + p2) * u + (2 * p0 - 5 * p1 + 4 * p2 - p3) * u * u + (-p0 + 3 * p1 - 3 * p2 + p3) * u ** 3)


def rail_pose(sec):
    """ステージ秒 sec のカメラ位置と、進行方向の yaw / pitch [度]（RailCameraController と同じく接線を向く）"""
    points, speed = load_rail()
    eye = rail_sample(points, sec * speed)
    ahead = rail_sample(points, sec * speed + 1e-3)
    d = ahead - eye
    yaw = math.degrees(math.atan2(d[0], d[2]))
    pitch = math.degrees(math.atan2(-d[1], math.hypot(d[0], d[2])))
    return eye, yaw, pitch


def shaft_path():
    """StagePlayScene と同じく 48〜66 秒を 16 点"""
    points, speed = load_rail()
    return [rail_sample(points, (48.0 + 18.0 * i / 15.0) * speed) for i in range(16)]


# ---------------------------------------------------------------------------
# カメラ・空・出力
# ---------------------------------------------------------------------------

def sky(dir_y):
    """タイトルの青空のおおまかな近似（線形）。水平線が白っぽく、上ほど青い"""
    t = np.clip(dir_y, 0, 1)[:, None]
    horizon = np.array([0.78, 0.86, 0.95])
    zenith = np.array([0.25, 0.5, 0.85])
    below = np.array(SKY_BELOW)
    up = horizon + (zenith - horizon) * np.sqrt(t)
    return np.where((dir_y < 0)[:, None], below, up)


def camera_rays(width, height, yaw, pitch, fov_y):
    """yaw は +Z から +X への回転、pitch は下向きが正 [rad]"""
    xs = (np.arange(width) + 0.5) / width * 2 - 1
    ys = 1 - (np.arange(height) + 0.5) / height * 2
    X, Y = np.meshgrid(xs, ys)
    th = math.tan(fov_y / 2)
    d = np.stack([X * th * width / height, Y * th, np.ones_like(X)], -1)
    cp, sp = math.cos(pitch), math.sin(pitch)
    d = np.stack([d[..., 0], d[..., 1] * cp - d[..., 2] * sp, d[..., 1] * sp + d[..., 2] * cp], -1)
    cy, sy = math.cos(yaw), math.sin(yaw)
    d = np.stack([d[..., 0] * cy + d[..., 2] * sy, d[..., 1], -d[..., 0] * sy + d[..., 2] * cy], -1)
    d /= np.linalg.norm(d, axis=-1, keepdims=True)
    return d.reshape(-1, 3)


def load_params(use_json, overrides):
    p = dict(DEFAULTS)
    if p.get("hv", 0) > 0:
        p.update(HV_DEFAULTS)
    if use_json and TUNING_PATH.exists():
        root = json.loads(TUNING_PATH.read_text(encoding="utf-8-sig"))
        saved = root.get("raymarchClouds", {})
        p.update({k: v for k, v in saved.items() if k in p})
        if p.get("hv", 0) > 0:
            p.update(HV_DEFAULTS)  # 試作中は保存済みの旧方式の値（形の大きさ等）で上書きしない
    for item in overrides:
        key, value = item.split("=", 1)
        if key not in p:
            sys.exit(f"知らないパラメータ: {key}（使えるもの: {', '.join(sorted(p))}）")
        p[key] = [float(v) for v in value.split(",")] if isinstance(p[key], list) else float(value)
    return p


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", type=Path, default=Path("cloud_preview.png"))
    ap.add_argument("--eye", type=float, nargs=3, default=[0.0, 900.0, 0.0], help="カメラ位置（既定はレールの開始位置）")
    ap.add_argument("--yaw", type=float, default=math.degrees(math.atan2(0.47, 0.88)), help="向き [度]（+Z から +X へ。既定はレールの最初の向き）")
    ap.add_argument("--pitch", type=float, default=0.0, help="見下ろす角度 [度]")
    ap.add_argument("--fov", type=float, default=0.45, help="縦の画角 [rad]（ステージの FovY）")
    ap.add_argument("--size", type=int, nargs=2, default=[480, 270], help="幅 高さ [px]")
    ap.add_argument("--at-sec", type=float, help="レール上のこの秒のカメラから見る（--eye/--yaw/--pitch を上書き。--pitch-add で足す）")
    ap.add_argument("--pitch-add", type=float, default=0.0, help="--at-sec の見下ろし角に足す [度]")
    ap.add_argument("--no-json", action="store_true", help="Tuning/StagePlay.json の値を読まず既定値で描く")
    ap.add_argument("--set", nargs="*", default=[], metavar="KEY=VALUE", help="パラメータを上書き（色は r,g,b）")
    args = ap.parse_args(argv)

    width, height = args.size
    p = load_params(not args.no_json, args.set)
    noise = Noise(pixel_angle=args.fov / height)
    eye, yaw, pitch = np.array(args.eye), args.yaw, args.pitch
    if args.at_sec is not None:
        eye, yaw, pitch = rail_pose(args.at_sec)
        pitch += args.pitch_add
        print(f"{args.at_sec:.1f}s: eye ({eye[0]:.0f}, {eye[1]:.0f}, {eye[2]:.0f}) yaw {yaw:.0f} pitch {pitch:.0f}")
    dirs = camera_rays(width, height, math.radians(yaw), math.radians(pitch), args.fov)
    sky_color = sky(dirs[:, 1])
    color, alpha = Clouds(p, noise, shaft_path()).render(eye, dirs, np.full(len(dirs), np.inf))
    image = sky_color * (1 - alpha[:, None]) + color * alpha[:, None]
    # 線形 → sRGB（ゲームはシーン RT が SRGB）
    srgb = np.clip(image, 0, 1) ** (1 / 2.2)
    Image.fromarray((srgb.reshape(height, width, 3) * 255).astype(np.uint8)).save(args.out)
    print(f"wrote {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
