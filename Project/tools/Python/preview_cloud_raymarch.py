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
CACHE_PATH = Path(tempfile.gettempdir()) / "preview_cloud_raymarch_noise.npz"

# CloudRaymarcher::Params の既定値（ゼノブレ式の面で使う分だけ）
DEFAULTS = dict(
    top=850.0, bottom=300.0, bottomFade=15.0, seaDepth=160.0, noiseFloor=0.3, coverage=0.22, sharpness=3.0,
    shapeScale=1800.0, mokoScale=500.0, mokoWeight=0.55, maskScale=9000.0, maskStrength=0.4,
    steps=48, firstStep=4.0, maxDistance=20000.0, probeLength=30.0,
    normalEps=12.0, wrap=0.3, lightStep=18.0, density=1.2, valley=0.7, valleyPow=0.6, ambient=0.15,
    litColor=[1.0, 0.99, 0.97], shadowColor=[0.30, 0.38, 0.55],
    traceSlope=0.35, traceEpsilon=0.5,
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


# ---------------------------------------------------------------------------
# ノイズとサンプリング
# ---------------------------------------------------------------------------

def load_noise():
    if CACHE_PATH.exists():
        d = np.load(CACHE_PATH)
        return d["shape"], d["mask"], d["moko"]
    print("ノイズを生成中（初回のみ）...", flush=True)
    shape, mask = gen.build(512, 1)
    moko = gen.build_moko(512, 1, gen.MOKO_RECIPES["large_soft"])
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
        self.pixel_angle = pixel_angle

    def sample(self, name, x, z, scale, dist):
        """距離 dist [m] とノイズ 1 枚の大きさ scale [m] からミップを選ぶ（レベル間は線形）"""
        levels = self.levels[name]
        texels = dist * self.pixel_angle * levels[0].shape[0] / scale
        lod = np.clip(np.log2(np.maximum(texels, 1.0)), 0, len(levels) - 1)
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
        large = self.noise.sample("shape", x, z, p["shapeScale"], dist)
        moko = self.noise.sample("moko", x, z, p["mokoScale"], dist)
        n = large + (moko - large) * p["mokoWeight"]
        return p["noiseFloor"] + (1 - p["noiseFloor"]) * n

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
        n = self.cloud_noise(pos[:, 0], pos[:, 2], dist)
        density = np.clip((p["top"] - pos[:, 1]) / p["seaDepth"], 0, 1) * np.clip((pos[:, 1] - p["bottom"]) / p["bottomFade"], 0, 1)
        sea = (n * density - self.coverage(pos[:, 0], pos[:, 2], dist)) * p["sharpness"]
        inside, c, d = self.hole_region(pos)
        if inside.any():
            # 穴：雲海の中で、穴の外側の雲（壁・天井・床）でも無い所
            sea[inside] = np.minimum(sea[inside], self.hole_over(pos[inside], dist[inside], c[inside], d[inside]))
        return sea

    def surface_height(self, x, z, dist):
        """上面の高さ H = 最高点 − 深さ × 補正値 / ノイズ（ノイズ × 濃さ = 補正値 の所）"""
        p = self.p
        n = np.maximum(self.cloud_noise(x, z, dist), 1e-3)
        depth = np.minimum(p["seaDepth"] * self.coverage(x, z, dist) / n, p["seaDepth"])
        return p["top"] - depth

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
        """上面の高さ場トレース（TraceSurface）。戻り値 (t, hit)"""
        p = self.p
        descent = -np.minimum(dirs[:, 1], -1e-4) + p["traceSlope"]
        t = t0.copy()
        active = active & (t < t_end)
        hit = np.zeros(len(t), bool)
        for _ in range(int(p["steps"])):
            if not active.any():
                break
            pos = eye + dirs * t[:, None]
            gap = pos[:, 1] - self.surface_height(pos[:, 0], pos[:, 2], t)
            done = active & (gap < p["traceEpsilon"])
            hit |= done
            active &= ~done
            t = np.where(active, t + np.maximum(gap, 0) / descent, t)
            active &= t < t_end
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
        eps = np.maximum(p["normalEps"], dist * self.noise.pixel_angle * 2)
        n0 = np.maximum(self.cloud_noise(pos[:, 0], pos[:, 2], dist), 1e-3)
        nx = self.cloud_noise(pos[:, 0] + eps, pos[:, 2], dist)
        nz = self.cloud_noise(pos[:, 0], pos[:, 2] + eps, dist)
        cov = self.coverage(pos[:, 0], pos[:, 2], dist)
        k = p["seaDepth"] * cov / (n0 * n0)
        normal = np.stack([-k * (nx - n0) / eps, np.ones_like(n0), -k * (nz - n0) / eps], -1)
        normal /= np.linalg.norm(normal, axis=-1, keepdims=True)

        # 上面の谷：こぶの高さの範囲の下ほど暗い
        depth_min = p["seaDepth"] * cov
        depth_max = p["seaDepth"] * cov / max(p["noiseFloor"], 1e-3)
        valley = np.clip((p["top"] - pos[:, 1] - depth_min) / np.maximum(depth_max - depth_min, 1), 0, 1)
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
        normal, ao, wall = self.surface_frame(pos, t)

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
    if use_json and TUNING_PATH.exists():
        root = json.loads(TUNING_PATH.read_text(encoding="utf-8-sig"))
        saved = root.get("raymarchClouds", {})
        p.update({k: v for k, v in saved.items() if k in p})
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
