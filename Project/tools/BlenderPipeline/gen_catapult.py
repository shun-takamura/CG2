"""スタート演出の八角形のリフト・縦穴・カタパルトを Blender で手続き生成するスクリプト。16_StageStart.md §2・§3 / F1。

使い方（Project/ で実行）:
    blender -b --factory-startup -P tools/BlenderPipeline/gen_catapult.py -- [オプション]
        --export <フォルダ>   部位ごとに glTF とテクスチャを書き出し、エンジンと共有する寸法の JSON も書く
                              （例: Assets/Models/Catapult。JSON は Resources/Json/Tuning/StageStartGeometry.json）
        --preview <フォルダ>  確認用の PNG（縦穴の中・広間へ降りる途中・射出路・後ろ左下から・縦穴を真上から）
        --with-mech           プレビューに HUB 用の機体を読み込む（遅い）
        --only <名前,...>     プレビューの構図を選ぶ（shaft / drop / clamp / hall / exit / exit_side / tps / top）
        --save <パス.blend>   生成したものを .blend で保存
        --ibl <パス.hdr>      リフトの上の機体の胸の高さからカタパルトの中を正距円筒で撮って IBL 用の .hdr にする
                              （例: Assets/catapult_ibl.hdr）。ネオンと液晶がチェック未完了（赤ピンク）のものをこの名前で、
                              完了（緑）のものを <名前>_ready.hdr で書く。明るさの倍率は 2 枚で同じ。--export なしで動く

方針（2026-10-15 ユーザー決定）:
    - リフト・神殿の床の穴・縦穴の断面は同じ八角形（正方形の角を落とした形）。半分の幅 OCT_H、角を落とす量 OCT_C
    - 縦穴とカタパルトはグレーの「いかにもカタパルト」（外と合わなければ白と金に変える）
    - カタパルトの左右にネオンパネル（`Neon`。チェック未完了は赤ピンク、完了で緑。色はエンジンが変える）
    - 断面（射出口も）は八角形。斜めの面に台形のネオンの板、左右の縦の壁に文字が流れる液晶の帯（2026-10-15 スケッチ）
    - 給電ワイヤーの付け根は機体の後ろの壁の高い所（カメラに映らない）

座標: gen_hub_temple.py と同じく形はすべて**エンジンの座標**（Y 上・正面 +Z）で組み、Blender へ入れるときに変える。

部位（書き出すファイル）。どれも回転なしで置く:
    StartLift          … 八角形のリフト。原点＝上面の中心（上面が y=0）。上面は神殿の床の模様
                         （glTF の画像は ../HubTemple/HubFloor_BaseColor.png を指す。UV は床と同じ x/44, z/44）
    StartShaftUnit     … 縦穴の繰り返しの単位。原点＝下の端の中心、高さ SHAFT_UNIT。上へ積んで、y を
                         SHAFT_UNIT で割った余りで流せば何周でも降り続けられる
    StartShaftTop      … 神殿の床のすぐ下の縁（大理石と金）。原点＝床の高さの中心、下へ SHAFT_TOP_DEPTH
    Catapult           … カタパルトの施設（後ろの壁から射出口まで同じ台形・床の 2 本の溝とレール・ネオンの板・
                         ワイヤーの付け根の箱）。原点＝リフトが止まる位置の中心、床が y=0。機体は +Z へ射出する
    CatapultLcd        … 左右の縦の壁の液晶の帯（LcdLeft / LcdRight。テクスチャ LcdLaunchUnable / LcdLaunchReady を差し替え、
                         uvTransform で U をずらして流す。左右で符号が逆）。原点は Catapult と同じ
    CatapultClamp      … 両脚の下の固定台（溝にはまる台車、上面 y=0）。原点＝足の中心の上面、(±RAIL_X, 0, CLAMP_Z) に 2 つ
    CatapultExitLight  … 射出口の奥の光る板（`Light` だけ。ID マスクの光芒・白フェードに使う）。原点は Catapult と同じ
マテリアル: Metal / MetalDark / Hazard / Light / Neon / Gold / Marble / HubFloor / LcdLeft / LcdRight /
            NeonRow00〜（斜めの面のネオンの列ごと。機体確認の後に 1 列ずつ点け直すため。固定台は Neon）
（全部に法線マップを付ける＝PSO が submesh[0] で決まる罠の対策。平らなものは Gold_NormalMap を共有）
"""

import json
import math
import sys
from pathlib import Path

import bpy
import numpy as np
from mathutils import Vector

sys.path.insert(0, str(Path(__file__).resolve().parent))
import gen_hub_temple as ht  # noqa: E402  形状・テクスチャ・書き出しの関数を使い回す

# ============================================================
# 寸法（エンジン座標、m）
# ============================================================
OCT_H = 10.0             # 八角形の半分の幅（機体の翼の先 |x| 8.63 から 1.37m 空く）
OCT_C = 1.0              # 角を落とす量（各軸で）。内側の判定: |x|<=H, |z|<=H, |x|+|z|<=2H-C

LIFT_THICK = 1.2
LIFT_RIM_W = 0.3         # 上面の縁の金の直線の幅
LIFT_RIM_RISE = 0.005    # 金の縁は上面より少し高い（z ファイティング対策）

SHAFT_UNIT = 8.0         # 縦穴の繰り返しの高さ。光の帯は単位ごとに 1 本
SHAFT_GAP = 0.02         # 壁の内側の面とリフトの縁の隙間
SHAFT_WALL_D = 0.35      # 板の奥（壁の地の面）までの深さ
SHAFT_BAND_Y = (3.78, 4.22)
SHAFT_TOP_DEPTH = 1.5

# カタパルトは後ろの壁から射出口まで同じ断面（2026-10-15 ユーザー指示で広間も射出路の延長にした）
# 2026-10-18 ユーザー指示で −14 → −32：三人称のカメラ（機体の後方・左下、z −26 あたり）が壁の中に埋まらないように
BACK_Z = -32.0           # 後ろの壁の内側
CEIL_T = 1.0             # 天井の厚さ。天井の上（y = TUN_H + CEIL_T）から縦穴が続く
FLOOR_T = 0.5
PIT_DEPTH = 3.0          # リフトが収まる穴の深さ

# 断面は八角形（2026-10-15 ユーザーのスケッチで台形から変更）：床・天井・左右の縦の壁・斜めの面 4 つ。射出口も同じ形。
# 床と天井の平らな所（半分の幅 TUN_HW − TUN_KX）にリフトと縦穴の八角形（半分の幅 OCT_H）を開けるので、OCT_H より大きくする
TUN_HW = 18.0            # 左右の縦の壁までの半分の幅
# 斜めの面（x 方向・y 方向）。2026-10-15 ユーザー指示で斜めの面を液晶の帯の際まで伸ばした（縦の壁＝液晶の帯の高さだけ）。
# 床・天井の平らな所（半分の幅 TUN_HW − TUN_KX）を保つため、x は 7 のまま y だけ伸ばす（45° ではない）
TUN_KX = 7.0
TUN_KY = 10.0
TUN_H = 25.0
TUN_END_Z = 160.0        # 射出口（枠の奥の面）
TUN_FRAME_STEP = 8.0     # ネオンの板を並べる区画
# 斜めの面のネオンの板。斜めの面を横切る台形で、平行な 2 辺は斜めの面を横切る向き。
# 後ろ（−Z）の辺が長く、進行方向（+Z）の辺が両端を TUN_NEON_FRONT_INSET ずつ詰めて短い（2026-10-15 ユーザーのスケッチ）
TUN_NEON_DZ = (-1.8, 1.8)    # 区画の中心からの z のずれ
TUN_NEON_W = 1.4             # 板の幅（z）
TUN_NEON_FRONT_INSET = 0.8   # 前の辺を両端から詰める量
TUN_NEON_MARGIN = (0.5, 0.8) # 斜めの面の両端（液晶の側・遠い側）から空ける量


def _neon_row_z():
    """ネオンの板の列の z（板の中心）。8m の区画ごとに 2 枚を並べ、並びの順に一つ飛ばし（2026-10-15 ユーザー指示で半分の数に）"""
    frames = list(np.arange(BACK_Z + TUN_FRAME_STEP * 0.5, TUN_END_Z - 2.0, TUN_FRAME_STEP))
    centers = [(fa + fb) * 0.5 for fa, fb in zip(frames[:-1], frames[1:])]
    return [float(c + dz) for c in centers for dz in TUN_NEON_DZ][::2]


# 列ごとにマテリアルを分ける（2026-10-19 F4 ユーザー決定：機体確認が済んだら、機体の位置から射出口へ 1 列ずつ緑で点け直す。
# エンジンはマテリアル名で列の発光を変える）。名前と z は StageStartGeometry.json の catapult.neonRows
NEON_ROW_Z = _neon_row_z()
NEON_ROW_NAMES = [f"NeonRow{k:02d}" for k in range(len(NEON_ROW_Z))]
# 左右の縦の壁の液晶の帯（黒い液晶の地にドットの文字。LAUNCH UNABLE＝赤ピンク / LAUNCH READY＝緑（2026-10-15 ユーザー指示で OK から変更）、進行方向へ流す）
LCD_LIP = 0.25           # 液晶を壁に埋め込んで見せる上下の縁の幅・張り出し
LCD_Y = (TUN_KY + LCD_LIP, TUN_H - TUN_KY - LCD_LIP)   # 4.5m（2026-10-15 ユーザー指示で 3m の 1.5 倍）
LCD_Z = (BACK_Z + 2.0, TUN_END_Z - 3.0)
LCD_COLS, LCD_ROWS, LCD_DOT_PX = 96, 11, 16        # 1 枚の文字の並び（5×7 のドット＋1 列の間 × 16 文字）
LCD_TILE_M = (LCD_Y[1] - LCD_Y[0]) * LCD_COLS / LCD_ROWS   # テクスチャ 1 枚分の長さ（ドットが正方形になる）
LCD_TEXTS = {"LcdLaunchUnable": ("LAUNCH UNABLE //", (1.0, 0.22, 0.48)), "LcdLaunchReady": ("LAUNCH READY ///", (0.25, 1.0, 0.45))}
EXIT_FRAME_IN = 1.2     # 射出口の枠が射出路の内側へ張り出す量
EXIT_LIGHT_Z = TUN_END_Z + 1.0

# レールは両脚の下に 2 本（足の中心 x ±3.0・z 1.2 は HubMech の頂点から実測）。床に掘った溝の中を通し、
# 固定台（CatapultClamp）の上面を床と同じ高さにする＝機体は HUB と同じ y=0 に立つ
RAIL_X = 3.0
GROOVE_HW = 0.6          # 溝の半分の幅
GROOVE_D = 0.35          # 溝の深さ
GROOVE_LIFT_Z0 = -1.6    # リフトの上の溝は足の後ろから前の縁まで
RAIL_START_Z = OCT_H     # カタパルトの床の溝はリフトの前の縁から
CLAMP_Z = 1.2            # 固定台の中心（足の中心）
CLAMP_HZ = 2.6           # 固定台の半分の長さ
CLAMP_JAW_HX = 1.1       # 足の前後の爪の半分の幅（溝より広く、床の少し上を滑る）
CLAMP_JAW_H = 0.5
CABLE_ANCHORS = [(3.0, 21.0, BACK_Z + 1.2), (-3.0, 21.0, BACK_Z + 1.2)]

_BASE_MAT_NAMES = ["Metal", "MetalDark", "Hazard", "Light", "Neon", "Gold", "Marble", "HubFloor", "LcdLeft", "LcdRight"]
(MAT_METAL, MAT_DARK, MAT_HAZARD, MAT_LIGHT, MAT_NEON, MAT_GOLD, MAT_MARBLE, MAT_FLOOR,
 MAT_LCD_L, MAT_LCD_R) = range(len(_BASE_MAT_NAMES))
# 斜めの面のネオンの列（固定台の状態の光は Neon のまま）
MAT_NEON_ROW0 = len(_BASE_MAT_NAMES)
MAT_NAMES = _BASE_MAT_NAMES + NEON_ROW_NAMES
HUB_FLOOR_URI = "../HubTemple/HubFloor_BaseColor.png"
GEOMETRY_JSON = Path("Resources/Json/Tuning/StageStartGeometry.json")
CLEARANCE_MIN = 0.5      # 機体と壁の最小の隙間

# IBL（2026-10-18 ユーザー決定：ネオン赤ピンク／緑の 2 枚、地下なので神殿の 0.45 より暗く）
IBL_EYE = (0.0, 12.0, 0.0)   # リフトの停止位置の上、機体の胸の高さ（神殿の IBL と同じ）
IBL_SIZE = (1024, 512)
IBL_SAMPLES = 128
IBL_MEAN = 0.25          # 赤ピンクの 1 枚の平均の明るさ（線形）。緑の 1 枚にも同じ倍率をかける
NEON_EMISSION = {"unable": (1.0, 0.18, 0.42), "ready": (0.25, 1.0, 0.45)}

LOG = "[gen_catapult]"
SQRT2 = math.sqrt(2.0)


# ============================================================
# 八角形と多角形
# ============================================================
def octagon(d=0.0):
    """八角形を外へ d だけ広げた頂点（x, z）。各辺が d 外へ出るので角を落とす量は C + d(2 − √2) になる"""
    h = OCT_H + d
    c = OCT_C + d * (2.0 - SQRT2)
    return [(h, -(h - c)), (h, h - c), (h - c, h), (-(h - c), h),
            (-h, h - c), (-h, -(h - c)), (-(h - c), -h), (h - c, -h)]


def octagon_sides(d=0.0):
    """辺ごとの (始点, 単位の向き, 外向きの法線, 長さ)。偶数番が長い辺"""
    pts = octagon(d)
    sides = []
    for i in range(8):
        a, b = np.array(pts[i]), np.array(pts[(i + 1) % 8])
        t = b - a
        length = float(np.linalg.norm(t))
        t /= length
        n = np.array([t[1], -t[0]])
        if np.dot(n, (a + b) * 0.5) < 0.0:
            n = -n
        sides.append((a, t, n, length))
    return sides


def octagon_sdf(x, z):
    """八角形の内側で負（壁までの距離）。エンジンの床の穴（F2）も同じ式で判定する"""
    return np.maximum(np.maximum(np.abs(x) - OCT_H, np.abs(z) - OCT_H),
                      (np.abs(x) + np.abs(z) - (2.0 * OCT_H - OCT_C)) / SQRT2)


def section(d=0.0):
    """カタパルトの断面（x, y）。床の辺（0 番）は動かさずに残りの辺を外へ d 広げた形（d < 0 で内へ）。
    頂点は床の左から反時計回り：床 → 右下の斜め → 右の壁 → 右上の斜め → 天井 → 左上 → 左の壁 → 左下"""
    w, kx, ky, h = TUN_HW, TUN_KX, TUN_KY, TUN_H
    pts = [(-(w - kx), 0.0), (w - kx, 0.0), (w, ky), (w, h - ky), (w - kx, h), (-(w - kx), h), (-w, h - ky), (-w, ky)]
    return offset_polygon(pts, [0.0] + [d] * 7)


def section_hw(y):
    """断面の高さ y での半分の幅"""
    w, kx, ky, h = TUN_HW, TUN_KX, TUN_KY, TUN_H
    return np.minimum(np.minimum(w - kx + kx * y / ky, w), w - kx + kx * (h - y) / ky)


def offset_polygon(pts, dists):
    """凸多角形の辺 i（pts[i]→pts[i+1]）を外へ dists[i] ずらし、隣の辺と交わる点を新しい頂点にする"""
    n = len(pts)
    center = np.mean(np.array(pts), axis=0)
    lines = []
    for i in range(n):
        a, b = np.array(pts[i]), np.array(pts[(i + 1) % n])
        t = (b - a) / np.linalg.norm(b - a)
        nrm = np.array([t[1], -t[0]])
        if np.dot(nrm, a - center) < 0.0:
            nrm = -nrm
        lines.append((a + nrm * dists[i], t))
    out = []
    for i in range(n):
        p0, t0 = lines[i - 1]
        p1, t1 = lines[i]
        m = np.array([t0, -t1]).T
        s = np.linalg.solve(m, p1 - p0)
        out.append(tuple(p0 + t0 * s[0]))
    return out


# ============================================================
# 形状のヘルパー（ht.MeshBuilder に頂点と面を足す）
# ============================================================
def extrude_xz(mb, pts, y0, y1, mat):
    """XZ の凸多角形を y0..y1 に押し出した立体。戻り値は (上面の面番号, 頂点の先頭)"""
    n = len(pts)
    verts = [(x, y0, z) for x, z in pts] + [(x, y1, z) for x, z in pts]
    faces = [list(range(n)), list(range(n, 2 * n))]
    for i in range(n):
        j = (i + 1) % n
        faces.append((i, j, n + j, n + i))
    first_face, first_vert = len(mb.faces), len(mb.verts)
    mb.add_solid(verts, faces, mat)
    return first_face + 1, first_vert


def ring_xz(mb, inner, outer, y0, y1, mat):
    """同じ点数の内・外の XZ の多角形の間を y0..y1 に押し出した輪"""
    n = len(inner)
    verts = ([(x, y0, z) for x, z in inner] + [(x, y0, z) for x, z in outer]
             + [(x, y1, z) for x, z in inner] + [(x, y1, z) for x, z in outer])
    ib, ob, it, ot = 0, n, 2 * n, 3 * n
    faces = []
    for i in range(n):
        j = (i + 1) % n
        faces += [(ib + i, ib + j, ob + j, ob + i), (it + i, ot + i, ot + j, it + j),
                  (ib + i, it + i, it + j, ib + j), (ob + i, ob + j, ot + j, ot + i)]
    mb.add_solid(verts, faces, mat)


def extrude_xy(mb, pts, z0, z1, mat):
    """XY の凸多角形を z0..z1 に押し出した立体（射出路の断面方向の部品）"""
    n = len(pts)
    verts = [(x, y, z0) for x, y in pts] + [(x, y, z1) for x, y in pts]
    faces = [list(range(n)), list(range(n, 2 * n))]
    for i in range(n):
        j = (i + 1) % n
        faces.append((i, j, n + j, n + i))
    mb.add_solid(verts, faces, mat)


def band_xy(mb, inner, outer, z0, z1, mat, closed):
    """XY の内・外の折れ線の間を z0..z1 に押し出した板。closed=False なら両端にふた（射出路の枠のコの字）"""
    n = len(inner)
    verts = ([(x, y, z0) for x, y in inner] + [(x, y, z0) for x, y in outer]
             + [(x, y, z1) for x, y in inner] + [(x, y, z1) for x, y in outer])
    ib, ob, it, ot = 0, n, 2 * n, 3 * n
    faces = []
    for i in range(n if closed else n - 1):
        j = (i + 1) % n
        faces += [(ib + i, ib + j, ob + j, ob + i), (it + i, ot + i, ot + j, it + j),
                  (ib + i, it + i, it + j, ib + j), (ob + i, ob + j, ot + j, ot + i)]
    if not closed:
        for k in (0, n - 1):
            faces.append((ib + k, ob + k, ot + k, it + k))
    mb.add_solid(verts, faces, mat)


def side_box(mb, side, s0, s1, d0, d1, y0, y1, mat):
    """八角形の辺に沿った箱。s は辺の始点からの距離、d は辺から外への距離"""
    a, t, n, _ = side
    c = []
    for y in (y0, y1):
        for s, d in ((s0, d0), (s1, d0), (s1, d1), (s0, d1)):
            p = a + t * s + n * d
            c.append((p[0], y, p[1]))
    mb.hexa(c, mat)


def aabb(mb, lo, hi, mat):
    mb.box(((lo[0] + hi[0]) * 0.5, (lo[1] + hi[1]) * 0.5, (lo[2] + hi[2]) * 0.5),
           ((hi[0] - lo[0]) * 0.5, (hi[1] - lo[1]) * 0.5, (hi[2] - lo[2]) * 0.5), 0.0, mat)


def slab_with_octagon_hole(mb, x0, x1, z0, z1, y0, y1, mat):
    """矩形の板から八角形（d=0）を抜いた形を、矩形の帯 4 枚と角の三角形 4 つで作る（床・天井）"""
    h = OCT_H
    for lo, hi in (((x0, z0), (-h, z1)), ((h, z0), (x1, z1)), ((-h, h), (h, z1)), ((-h, z0), (h, -h))):
        if hi[0] - lo[0] > 1e-6 and hi[1] - lo[1] > 1e-6:
            aabb(mb, (lo[0], y0, lo[1]), (hi[0], y1, hi[1]), mat)
    c = h - OCT_C
    for sx, sz in ((1, 1), (-1, 1), (-1, -1), (1, -1)):
        extrude_xz(mb, [(sx * h, sz * h), (sx * c, sz * h), (sx * h, sz * c)], y0, y1, mat)


def clip_axis(pts, axis, lo, hi):
    """凸多角形（2D）を axis（0 or 1）の lo..hi で切り取る（Sutherland–Hodgman）"""
    def clip(poly, keep, edge):
        out = []
        for i in range(len(poly)):
            a, b = poly[i - 1], poly[i]
            ia, ib = keep(a), keep(b)
            if ib:
                if not ia:
                    out.append(edge(a, b))
                out.append(b)
            elif ia:
                out.append(edge(a, b))
        return out

    def cut(v):
        def edge(a, b):
            t = (v - a[axis]) / (b[axis] - a[axis])
            return tuple(a[k] + (b[k] - a[k]) * t for k in range(2))
        return edge

    poly = clip(list(pts), lambda p: p[axis] >= lo - 1e-9, cut(lo)) if lo is not None else list(pts)
    if poly and hi is not None:
        poly = clip(poly, lambda p: p[axis] <= hi + 1e-9, cut(hi))
    return poly if len(poly) >= 3 else []


GROOVES = [(-RAIL_X - GROOVE_HW, -RAIL_X + GROOVE_HW), (RAIL_X - GROOVE_HW, RAIL_X + GROOVE_HW)]


def x_without_grooves(x0, x1):
    """x0..x1 から 2 本の溝を除いた区間"""
    cuts = [x0] + [v for g in GROOVES for v in g] + [x1]
    return [(cuts[i], cuts[i + 1]) for i in range(0, len(cuts), 2)]


def ring_split(mb, d0, d1, y0, y1, mat):
    """八角形の輪（d0..d1）。前の辺（z = +H）だけ溝の所を抜いて、固定台が前へ抜けられるようにする"""
    pi, po = octagon(d0), octagon(d1)
    for i in range(8):
        j = (i + 1) % 8
        quad = [pi[i], pi[j], po[j], po[i]]
        pieces = [quad]
        if i == 2:
            pieces = [clip_axis(quad, 0, a, b) for a, b in x_without_grooves(-OCT_H - d1, OCT_H + d1)]
        for poly in pieces:
            if poly:
                extrude_xz(mb, poly, y0, y1, mat)


# ============================================================
# リフト
# ============================================================
def build_lift(mats):
    mb = ht.MeshBuilder()
    inner = octagon(-LIFT_RIM_W)

    def floor_top(poly, y_bottom):
        # 上面は神殿の床の模様。床の PS と同じく u = x/44・v = z/44（cook の V 反転と flip_texcoord_v を通した後の値）
        top, v0 = extrude_xz(mb, poly, y_bottom, 0.0, MAT_DARK)
        mb.mats[top] = MAT_FLOOR
        n = len(poly)
        mb.uvs[top] = {v0 + n + i: (x / ht.FLOOR_TILE_M, 1.0 - z / ht.FLOOR_TILE_M) for i, (x, z) in enumerate(poly)}

    # 溝の外は厚さいっぱい、溝の所は足の後ろまでだけ上面があり、その前は溝の底（固定台が前へ抜ける）
    for a, b in x_without_grooves(-OCT_H, OCT_H):
        poly = clip_axis(inner, 0, a, b)
        if poly:
            floor_top(poly, -LIFT_THICK)
    for g0, g1 in GROOVES:
        strip = clip_axis(inner, 0, g0, g1)
        floor_top(clip_axis(strip, 1, None, GROOVE_LIFT_Z0), -LIFT_THICK)
        extrude_xz(mb, clip_axis(strip, 1, GROOVE_LIFT_Z0, None), -LIFT_THICK, -GROOVE_D, MAT_DARK)
        build_groove_inside(mb, (g0 + g1) * 0.5, GROOVE_LIFT_Z0, OCT_H)
    ring_split(mb, -LIFT_RIM_W, 0.0, -0.05, LIFT_RIM_RISE, MAT_GOLD)
    ring_split(mb, -LIFT_RIM_W, 0.0, -GROOVE_D, -0.05, MAT_DARK)
    ring_xz(mb, inner, octagon(0.0), -LIFT_THICK, -GROOVE_D, MAT_DARK)
    # 裏の梁（降りる途中・広間の上からは見えないので最小限）
    for x in (-6.0, 0.0, 6.0):
        aabb(mb, (x - 0.4, -LIFT_THICK - 0.8, -OCT_H + 1.5), (x + 0.4, -LIFT_THICK, OCT_H - 1.5), MAT_DARK)
    aabb(mb, (-OCT_H + 1.5, -LIFT_THICK - 0.8, -0.4), (OCT_H - 1.5, -LIFT_THICK, 0.4), MAT_DARK)
    return mb.build("StartLift", mats)


# ============================================================
# 縦穴
# ============================================================
def build_shaft_unit(mats):
    mb = ht.MeshBuilder()
    d_face = SHAFT_GAP + 0.02     # 板の表
    d_wall = SHAFT_WALL_D
    ring_xz(mb, octagon(d_wall), octagon(d_wall + 0.55), 0.0, SHAFT_UNIT, MAT_DARK)
    b0, b1 = SHAFT_BAND_Y
    rows = [(0.06, b0 - 0.16), (b1 + 0.16, SHAFT_UNIT - 0.06)]
    # 光の帯（奥まった所）とその上下のリップ
    ring_xz(mb, octagon(d_face + 0.10), octagon(d_wall), b0, b1, MAT_LIGHT)
    ring_xz(mb, octagon(d_face), octagon(d_wall), b0 - 0.16, b0, MAT_DARK)
    ring_xz(mb, octagon(d_face), octagon(d_wall), b1, b1 + 0.16, MAT_DARK)
    for k, side in enumerate(octagon_sides(0.0)):
        length = side[3]
        if k % 2 == 1:
            # 角を落とした辺は柱だけ
            for y0, y1 in ((0.0, b0 - 0.16), (b1 + 0.16, SHAFT_UNIT)):
                side_box(mb, side, -0.05, length + 0.05, SHAFT_GAP, d_wall, y0, y1, MAT_DARK)
            continue
        cuts = [0.0, 0.35, length / 3.0 - 0.2, length / 3.0 + 0.2, 2.0 * length / 3.0 - 0.2,
                2.0 * length / 3.0 + 0.2, length - 0.35, length]
        for y0, y1 in rows:
            # 柱（端と板の間）
            for s0, s1 in ((cuts[0], cuts[1]), (cuts[2], cuts[3]), (cuts[4], cuts[5]), (cuts[6], cuts[7])):
                side_box(mb, side, s0, s1, SHAFT_GAP, d_wall, y0 - 0.06, y1 + 0.06, MAT_DARK)
            # 板と、その内側に一段浮かせた板
            for s0, s1 in ((cuts[1], cuts[2]), (cuts[3], cuts[4]), (cuts[5], cuts[6])):
                side_box(mb, side, s0 + 0.04, s1 - 0.04, d_face, d_wall, y0, y1, MAT_METAL)
                side_box(mb, side, s0 + 0.45, s1 - 0.45, d_face - 0.015, d_face, y0 + 0.4, y1 - 0.4, MAT_METAL)
    return mb.build("StartShaftUnit", mats)


def build_shaft_top(mats):
    """神殿の床のすぐ下の縁。上面は床（WaterSurface）の 1cm 下に隠す"""
    mb = ht.MeshBuilder()
    top = -0.01
    ring_xz(mb, octagon(0.0), octagon(0.8), -0.25, top, MAT_GOLD)
    ring_xz(mb, octagon(0.0), octagon(0.8), -SHAFT_TOP_DEPTH, -0.25, MAT_MARBLE)
    ring_xz(mb, octagon(-0.0), octagon(0.8), -SHAFT_TOP_DEPTH - 0.12, -SHAFT_TOP_DEPTH, MAT_GOLD)
    return mb.build("StartShaftTop", mats)


# ============================================================
# カタパルト
# ============================================================
def build_groove_inside(mb, xc, z0, z1):
    """溝の底のレールと給電の線（雷はここから固定台へ走らせる）。
    2026-10-19 F5：線を光らせると電磁カタパルトの雷が見えにくかったので、照明（Light）から暗い鋼に替えた"""
    aabb(mb, (xc - 0.1, -GROOVE_D, z0), (xc + 0.1, -GROOVE_D + 0.15, z1), MAT_METAL)
    for sx in (-1.0, 1.0):
        x = xc + sx * 0.35
        aabb(mb, (x - 0.04, -GROOVE_D, z0), (x + 0.04, -GROOVE_D + 0.01, z1), MAT_DARK)


def section_outer(inner):
    return offset_polygon(inner, [FLOOR_T] + [1.0] * 7)


def u_band(mb, z0, z1, mat):
    """断面の床の辺を除いた壁と天井。厚さは外へ 1m"""
    inner = section(0.0)
    outer = section_outer(inner)
    band_xy(mb, inner[1:] + inner[:1], outer[1:] + outer[:1], z0, z1, mat, closed=False)


def build_shell(mb):
    """後ろの壁から射出口まで同じ断面。リフトの所（|z| <= OCT_H）だけ床と天井に八角形の穴"""
    h = OCT_H
    inner = section(0.0)
    outer = section_outer(inner)
    fx = TUN_HW - TUN_KX + 1.5
    # 後ろの壁
    extrude_xy(mb, outer, BACK_Z - 1.0, BACK_Z, MAT_METAL)
    # リフトの後ろ・前は壁と天井のコの字、リフトの所は左右の壁と穴の開いた天井
    u_band(mb, BACK_Z, -h, MAT_METAL)
    u_band(mb, h, TUN_END_Z, MAT_METAL)
    band_xy(mb, inner[1:5], outer[1:5], -h, h, MAT_METAL, closed=False)
    band_xy(mb, inner[5:] + inner[:1], outer[5:] + outer[:1], -h, h, MAT_METAL, closed=False)
    tx = TUN_HW - TUN_KX + 1.0
    slab_with_octagon_hole(mb, -tx, tx, -h, h, TUN_H, TUN_H + CEIL_T, MAT_METAL)
    # 床：後ろはそのまま、リフトの所は八角形の穴、前は 2 本の溝
    aabb(mb, (-fx, -FLOOR_T, BACK_Z), (fx, 0.0, -h), MAT_DARK)
    slab_with_octagon_hole(mb, -fx, fx, -h, h, -FLOOR_T, 0.0, MAT_DARK)
    z1 = TUN_END_Z
    for a, b in x_without_grooves(-fx, fx):
        aabb(mb, (a, -FLOOR_T, h), (b, 0.0, z1), MAT_DARK)
    for g0, g1 in GROOVES:
        aabb(mb, (g0, -FLOOR_T, h), (g1, -GROOVE_D, z1), MAT_DARK)
        build_groove_inside(mb, (g0 + g1) * 0.5, h, z1 - 2.0)
        # 溝の両脇の注意の縞
        aabb(mb, (g0 - 0.3, 0.0, h + 0.8), (g0, 0.006, z1 - 2.0), MAT_HAZARD)
        aabb(mb, (g1, 0.0, h + 0.8), (g1 + 0.3, 0.006, z1 - 2.0), MAT_HAZARD)
    # リフトが収まる穴と注意の縞（前の辺は溝の所を抜く）
    ring_xz(mb, octagon(0.0), octagon(0.5), -FLOOR_T - PIT_DEPTH, -FLOOR_T, MAT_DARK)
    extrude_xz(mb, octagon(0.5), -FLOOR_T - PIT_DEPTH - 0.2, -FLOOR_T - PIT_DEPTH, MAT_DARK)
    ring_split(mb, 0.0, 0.8, 0.0, 0.006, MAT_HAZARD)
    # 天井の穴の縁（縦穴の出口）
    ring_xz(mb, octagon(0.0), octagon(0.4), TUN_H - 0.6, TUN_H, MAT_DARK)
    ring_xz(mb, octagon(0.02), octagon(0.4), TUN_H - 1.0, TUN_H - 0.6, MAT_LIGHT)
    # 後ろの壁の高い所：給電ワイヤーの付け根の箱
    for ax, ay, az in CABLE_ANCHORS:
        aabb(mb, (ax - 1.0, ay - 1.0, BACK_Z), (ax + 1.0, ay + 3.0, az), MAT_DARK)
        aabb(mb, (ax - 0.7, ay - 1.02, BACK_Z + 0.2), (ax + 0.7, ay - 1.0, az - 0.2), MAT_LIGHT)


def build_details(mb):
    z0, z1 = BACK_Z, TUN_END_Z
    # 足元の誘導灯（リフトの前から）
    for sx in (-1.0, 1.0):
        for z in np.arange(OCT_H + 3.0, z1 - 2.0, 4.0):
            aabb(mb, (sx * 8.0 - 0.25, 0.0, z - 0.4), (sx * 8.0 + 0.25, 0.06, z + 0.4), MAT_LIGHT)
    # 斜めの面 4 つのネオンの板（列 k の 4 枚は NeonRow{k}）
    sec = section(0.0)
    mid_c = np.array([0.0, TUN_H * 0.5])
    for i in (1, 3, 5, 7):
        a, b = np.array(sec[i]), np.array(sec[(i + 1) % 8])
        near, far = (a, b) if abs(abs(a[0]) - TUN_HW) < 1e-6 else (b, a)   # 液晶の側の端（縦の壁との角）
        t = (far - near) / np.linalg.norm(far - near)
        n = np.array([-t[1], t[0]])
        if np.dot(n, mid_c - (a + b) * 0.5) < 0.0:
            n = -n
        p_near = near + t * TUN_NEON_MARGIN[0]
        p_far = far - t * TUN_NEON_MARGIN[1]
        for k, z in enumerate(NEON_ROW_Z):
            # 面とほぼ同じ高さに置いて埋め込みに見せる（2026-10-15 ユーザー指示）。暗い縁 → ネオンの面の順に 5mm ずつ
            neon_panel(mb, p_near, p_far, n, z, 0.08, -0.05, 0.005, MAT_DARK)
            neon_panel(mb, p_near, p_far, n, z, 0.0, -0.05, 0.01, MAT_NEON_ROW0 + k)
    # 射出口の枠（断面と同じ八角形。射出路より内へ張り出す）
    rot = lambda pts: pts[1:] + pts[:1]  # noqa: E731  床の辺を除いた並び
    band_xy(mb, rot(section(-EXIT_FRAME_IN)), rot(section(0.0)), z1 - 2.0, z1, MAT_DARK, closed=False)
    band_xy(mb, rot(section(-EXIT_FRAME_IN - 0.05)), rot(section(-EXIT_FRAME_IN + 0.3)), z1 - 2.1, z1 - 2.0, MAT_HAZARD, closed=False)


def neon_panel(mb, p_near, p_far, n, zc, grow, d0, d1, mat):
    """斜めの面を横切る台形の板。後ろの辺（z = zc − W/2）は p_near〜p_far いっぱい、前の辺（z = zc + W/2）は
    両端を TUN_NEON_FRONT_INSET ずつ詰めて短い。grow で一回り大きく（枠）"""
    tn = (p_far - p_near) / np.linalg.norm(p_far - p_near)
    zb, zf = zc - TUN_NEON_W * 0.5 - grow, zc + TUN_NEON_W * 0.5 + grow
    bn, bf = p_near - tn * grow, p_far + tn * grow
    inset = TUN_NEON_FRONT_INSET - grow * 0.4
    fn, ff = bn + tn * inset, bf - tn * inset
    verts = []
    for d in (d0, d1):
        for q, z in ((bn, zb), (bf, zb), (ff, zf), (fn, zf)):
            r = q + n * d
            verts.append((r[0], r[1], z))
    mb.hexa(verts, mat)


def build_lcd(mats):
    """左右の縦の壁の液晶の帯。文字が左右とも鏡にならないよう U の向きが左右で逆＝流すときの符号も逆。
    右（LcdRight）は u = −z / LCD_TILE_M、左（LcdLeft）は u = +z / LCD_TILE_M"""
    mb = ht.MeshBuilder()
    z0, z1 = LCD_Z
    y0, y1 = LCD_Y
    for sx, mat in ((-1.0, MAT_LCD_L), (1.0, MAT_LCD_R)):
        wall = sx * TUN_HW
        # 上下と両端に縁を張り出させ、液晶の面は壁の面に置く＝壁に埋め込まれて見える（2026-10-15 ユーザー指示）
        xi = wall - sx * LCD_LIP
        lo_x, hi_x = min(wall, xi), max(wall, xi)
        aabb(mb, (lo_x, y0 - LCD_LIP, z0 - LCD_LIP), (hi_x, y0, z1 + LCD_LIP), MAT_DARK)
        aabb(mb, (lo_x, y1, z0 - LCD_LIP), (hi_x, y1 + LCD_LIP, z1 + LCD_LIP), MAT_DARK)
        aabb(mb, (lo_x, y0, z0 - LCD_LIP), (hi_x, y1, z0), MAT_DARK)
        aabb(mb, (lo_x, y0, z1), (hi_x, y1, z1 + LCD_LIP), MAT_DARK)
        x = wall - sx * 0.01
        verts = [(x, y0, z0), (x, y0, z1), (x, y1, z1), (x, y1, z0)]
        uvs = [(-sx * z / LCD_TILE_M, 0.0 if y == y0 else 1.0) for _, y, z in verts]
        mb.poly_facing(verts, uvs, mat, (-sx, 0.0, 0.0))
    return mb.build("CatapultLcd", mats)


def build_catapult(mats):
    mb = ht.MeshBuilder()
    build_shell(mb)
    build_details(mb)
    return mb.build("Catapult", mats)


def build_clamp(mats):
    """両脚の下の固定台。溝にはまる台車で、上面は床と同じ y=0。原点＝足の中心の上面（置く位置は (±RAIL_X, 0, CLAMP_Z)）。
    発進でこのまま前へ走らせられるよう、施設とは別のモデルにした"""
    mb = ht.MeshBuilder()
    hw = GROOVE_HW - 0.05
    aabb(mb, (-hw, -GROOVE_D + 0.17, -CLAMP_HZ), (hw, 0.0, CLAMP_HZ), MAT_DARK)
    for sz in (-1.0, 1.0):
        # 足の前後の爪（底は床の 1cm 上）と、内側の状態の光（Neon。チェックの色に合わせられる）
        z_in, z_out = sz * (CLAMP_HZ - 0.55), sz * (CLAMP_HZ - 0.15)
        lo, hi = min(z_in, z_out), max(z_in, z_out)
        aabb(mb, (-CLAMP_JAW_HX, 0.01, lo), (CLAMP_JAW_HX, CLAMP_JAW_H, hi), MAT_DARK)
        aabb(mb, (-hw, -0.02, lo), (hw, 0.01, hi), MAT_DARK)
        face = z_in - sz * 0.02
        aabb(mb, (-CLAMP_JAW_HX + 0.15, 0.15, min(face, z_in)), (CLAMP_JAW_HX - 0.15, 0.3, max(face, z_in)), MAT_NEON)
        aabb(mb, (-CLAMP_JAW_HX - 0.01, 0.01, lo + 0.05), (-CLAMP_JAW_HX + 0.2, 0.08, hi - 0.05), MAT_HAZARD)
        aabb(mb, (CLAMP_JAW_HX - 0.2, 0.01, lo + 0.05), (CLAMP_JAW_HX + 0.01, 0.08, hi - 0.05), MAT_HAZARD)
    return mb.build("CatapultClamp", mats)


def build_exit_light(mats):
    mb = ht.MeshBuilder()
    pts = section(-EXIT_FRAME_IN + 0.4)
    verts = [(x, y, EXIT_LIGHT_Z) for x, y in pts]
    mb.poly_facing(verts, [(0.5, 0.5)] * len(verts), MAT_LIGHT, (0.0, 0.0, -1.0))
    return mb.build("CatapultExitLight", mats)


# ============================================================
# 確認（機体が八角形と射出路に収まるか）
# ============================================================
def check_clearance():
    pts = ht.load_mech_points()
    if pts is None:
        return
    x, y, z = pts[:, 0], pts[:, 1], pts[:, 2]
    oct_gap = -float(np.max(octagon_sdf(x, z)))
    hw = section_hw(np.clip(y, 0.0, TUN_H))
    tun_side = float(np.min(hw - np.abs(x)))
    tun_top = TUN_H - float(y.max())
    print(f"{LOG} 機体と八角形の隙間 {oct_gap:.2f}m / 射出路の横 {tun_side:.2f}m・上 {tun_top:.2f}m")
    worst = min(oct_gap, tun_side, tun_top)
    if worst < CLEARANCE_MIN:
        raise SystemExit(f"{LOG} 機体が壁に近すぎる（{worst:.2f}m < {CLEARANCE_MIN}m）。OCT_* / TUN_* を見直す")


# ============================================================
# テクスチャとマテリアル
# ============================================================
def metal_fields(n, seed, base_srgb, variation, streak):
    """塗装した鋼板（むら・細かい粒・縦に流れた汚れ）。継ぎ目なしで繰り返す"""
    rng = np.random.default_rng(seed)
    cloud = ht.tileable_noise(rng, n, 1.7)
    grain = ht.tileable_noise(rng, n, 0.6)
    raw = ht.tileable_noise(rng, n, 1.2)
    drip = sum(np.roll(raw, k, axis=0) for k in range(-48, 49, 6)) / 17.0
    drip = (drip - drip.min()) / (drip.max() - drip.min())
    color = (np.array(base_srgb)[None, None, :] + (cloud[..., None] - 0.5) * variation
             + (grain[..., None] - 0.5) * variation * 0.5 - np.clip(drip - 0.55, 0.0, 1.0)[..., None] * streak)
    height = grain * 0.15 + cloud * 0.05
    return np.clip(color, 0.0, 1.0), height


def hazard_fields(n, seed):
    """黄と黒の斜めの縞（2m のタイルに 4 本）に擦れ"""
    rng = np.random.default_rng(seed)
    v, u = np.mgrid[0:n, 0:n] / n
    stripe = ((u + v) * 4.0) % 1.0 < 0.5
    wear = ht.tileable_noise(rng, n, 1.4)
    yellow, black = np.array([0.93, 0.72, 0.10]), np.array([0.07, 0.07, 0.07])
    color = np.where(stripe[..., None], yellow, black)
    rub = np.clip((wear - 0.62) * 3.0, 0.0, 1.0)[..., None] * 0.5
    return np.clip(color * (1.0 - rub) + np.array([0.35, 0.34, 0.33]) * rub, 0.0, 1.0)


# 液晶の 5×7 ドットの文字（1 = 点灯）
DOT_FONT = {
    "A": ["01110", "10001", "10001", "11111", "10001", "10001", "10001"],
    "B": ["11110", "10001", "10001", "11110", "10001", "10001", "11110"],
    "C": ["01110", "10001", "10000", "10000", "10000", "10001", "01110"],
    "D": ["11100", "10010", "10001", "10001", "10001", "10010", "11100"],
    "E": ["11111", "10000", "10000", "11110", "10000", "10000", "11111"],
    "H": ["10001", "10001", "10001", "11111", "10001", "10001", "10001"],
    "L": ["10000", "10000", "10000", "10000", "10000", "10000", "11111"],
    "N": ["10001", "11001", "10101", "10011", "10001", "10001", "10001"],
    "R": ["11110", "10001", "10001", "11110", "10100", "10010", "10001"],
    "U": ["10001", "10001", "10001", "10001", "10001", "10001", "01110"],
    "Y": ["10001", "10001", "01010", "00100", "00100", "00100", "00100"],
    "/": ["00001", "00010", "00010", "00100", "01000", "01000", "10000"],
    " ": ["00000"] * 7,
}


def lcd_fields(text, color):
    """黒い液晶の地にドットの文字（1 文字 = 5 列＋間 1 列、上下 2 行の余白）。横に継ぎ目なく繰り返す"""
    assert len(text) * 6 == LCD_COLS, text
    lit = np.zeros((LCD_ROWS, LCD_COLS))
    for k, ch in enumerate(text):
        for r, row in enumerate(DOT_FONT[ch]):
            for c, bit in enumerate(row):
                lit[2 + r, k * 6 + c] = float(bit == "1")
    px = LCD_DOT_PX
    yy, xx = np.mgrid[0:px, 0:px] + 0.5
    dot = 1.0 - ht.smooth_step(px * 0.32, px * 0.42, np.hypot(xx - px * 0.5, yy - px * 0.5))   # 角の丸い点
    cell = np.kron(lit, dot)
    unlit = np.kron(np.ones_like(lit), dot) * 0.06
    col = np.array(color)[None, None, :]
    base = np.array([0.015, 0.017, 0.02])[None, None, :]
    return np.clip(base + col * cell[..., None] + col * unlit[..., None], 0.0, 1.0)


def make_textures(out_dir):
    out_dir.mkdir(parents=True, exist_ok=True)
    # 2026-10-15 ユーザー指示で暗めのグレーに（0.50 → 0.24、暗い方 0.17 → 0.10）
    metal, mh = metal_fields(ht.TEX_SIZE, 41, (0.24, 0.25, 0.27), 0.05, 0.08)
    dark, dh = metal_fields(ht.TEX_SIZE, 43, (0.10, 0.105, 0.11), 0.04, 0.04)
    # 縦穴の上の縁の大理石は神殿の白い大理石と同じ作り方（gen_hub_temple.py の make_textures と同じ引数）
    marble, mah = ht.marble_fields(ht.TEX_SIZE, 7, (0.95, 0.94, 0.97), (0.72, 0.70, 0.88), 0.4, 4.0, (0.92, 0.85, 0.96))
    flat = np.tile(np.array([0.5, 0.5, 1.0]), (4, 4, 1))
    return {
        "Metal": ht.save_png("Metal_BaseColor", metal, out_dir, False),
        "Metal_n": ht.save_png("Metal_NormalMap", ht.height_to_normal(mh, 0.15), out_dir, True),
        "MetalDark": ht.save_png("MetalDark_BaseColor", dark, out_dir, False),
        "MetalDark_n": ht.save_png("MetalDark_NormalMap", ht.height_to_normal(dh, 0.15), out_dir, True),
        "Hazard": ht.save_png("Hazard_BaseColor", hazard_fields(512, 47), out_dir, False),
        "Light": ht.save_png("Light_BaseColor", np.tile(np.array([1.0, 0.98, 0.95]), (4, 4, 1)), out_dir, False),
        "Neon": ht.save_png("Neon_BaseColor", np.tile(np.array([1.0, 1.0, 1.0]), (4, 4, 1)), out_dir, False),
        "Marble": ht.save_png("Marble_BaseColor", marble, out_dir, False),
        "Marble_n": ht.save_png("Marble_NormalMap", ht.height_to_normal(mah, 0.25), out_dir, True),
        "flat_n": ht.save_png("Gold_NormalMap", flat, out_dir, True),
        **{name: ht.save_png(name, lcd_fields(text, color), out_dir, False) for name, (text, color) in LCD_TEXTS.items()},
    }


def make_materials(tex):
    flat = tex["flat_n"]
    gold = bpy.data.materials.new("Gold")
    gold.use_nodes = True
    g = gold.node_tree.nodes["Principled BSDF"]
    g.inputs["Base Color"].default_value = (*ht.GOLD_SRGB, 1.0)
    g.inputs["Metallic"].default_value = 1.0
    g.inputs["Roughness"].default_value = 0.28
    ht.attach_normal_map(gold.node_tree, g, flat)

    # 床の模様は書き出した後に glTF へ足す（patch_lift_floor）。プレビューでは神殿の床の PNG があれば貼る
    floor = bpy.data.materials.new("HubFloor")
    floor.use_nodes = True
    f = floor.node_tree.nodes["Principled BSDF"]
    f.inputs["Base Color"].default_value = (1.0, 1.0, 1.0, 1.0)
    f.inputs["Roughness"].default_value = 0.1
    f.inputs["Metallic"].default_value = 0.0
    ht.attach_normal_map(floor.node_tree, f, flat)

    metal = ht.textured_material("Metal", tex["Metal"], tex["Metal_n"], 0.55)
    dark = ht.textured_material("MetalDark", tex["MetalDark"], tex["MetalDark_n"], 0.45)
    dark.node_tree.nodes["Principled BSDF"].inputs["Metallic"].default_value = 0.5
    neon = ht.textured_material("Neon", tex["Neon"], flat, 0.3, emission=4.0)
    # プレビューだけチェック未完了の赤ピンクで光らせる（エンジンでは色を変える）
    neon.node_tree.nodes["Principled BSDF"].inputs["Emission Color"].default_value = (1.0, 0.18, 0.42, 1.0)
    for link in list(neon.node_tree.links):
        if link.to_socket.name == "Emission Color":
            neon.node_tree.links.remove(link)
    mats = {
        "Metal": metal,
        "MetalDark": dark,
        "Hazard": ht.textured_material("Hazard", tex["Hazard"], flat, 0.6),
        "Light": ht.textured_material("Light", tex["Light"], flat, 0.5, emission=6.0),
        "Neon": neon,
        "Gold": gold,
        "Marble": ht.textured_material("Marble", tex["Marble"], tex["Marble_n"], 0.3),
        "HubFloor": floor,
        # 液晶は発進不可の文字を既定にする（エンジンで LcdLaunchReady に差し替える）
        "LcdLeft": ht.textured_material("LcdLeft", tex["LcdLaunchUnable"], flat, 0.2, emission=3.0),
        "LcdRight": ht.textured_material("LcdRight", tex["LcdLaunchUnable"], flat, 0.2, emission=3.0),
    }
    for name in NEON_ROW_NAMES:
        row = neon.copy()
        row.name = name
        mats[name] = row
    return [mats[n] for n in MAT_NAMES]


def preview_floor_texture(mats):
    path = Path("Assets/Models/HubTemple/HubFloor_BaseColor.png")
    if not path.exists():
        return
    floor = mats[MAT_FLOOR]
    nt = floor.node_tree
    tex = nt.nodes.new("ShaderNodeTexImage")
    tex.image = bpy.data.images.load(str(path.resolve()))
    nt.links.new(tex.outputs["Color"], nt.nodes["Principled BSDF"].inputs["Base Color"])


# ============================================================
# 書き出し
# ============================================================
def patch_lift_floor(gltf_path):
    """StartLift の HubFloor に神殿の床の模様（../HubTemple/）を付ける。cook は uri を正規化して
    Resources/Models/HubTemple/HubFloor_BaseColor.dds を指す .mat を書くので、テクスチャは重複しない"""
    g = json.loads(gltf_path.read_text(encoding="utf-8"))
    mats = [i for i, m in enumerate(g.get("materials", [])) if m.get("name") == "HubFloor"]
    if not mats:
        raise SystemExit(f"{LOG} {gltf_path} に HubFloor が無い")
    g.setdefault("images", []).append({"uri": HUB_FLOOR_URI})
    g.setdefault("samplers", [{}])
    g.setdefault("textures", []).append({"source": len(g["images"]) - 1, "sampler": 0})
    pbr = g["materials"][mats[0]].setdefault("pbrMetallicRoughness", {})
    pbr["baseColorTexture"] = {"index": len(g["textures"]) - 1}
    gltf_path.write_text(json.dumps(g, indent=2), encoding="utf-8")
    print(f"{LOG} patched {gltf_path.name}: HubFloor -> {HUB_FLOOR_URI}")


def write_geometry_json():
    """エンジン（F2 の床の穴・F3 の縦穴とカタパルト）と共有する寸法。値の元はこのスクリプトの定数"""
    data = {
        "_comment": "gen_catapult.py が書き出す。手で直さず、スクリプトの定数を直して --export する（16_StageStart.md §2・§3）",
        "octagon": {"halfWidth": OCT_H, "chamfer": OCT_C},
        "lift": {"thickness": LIFT_THICK, "rimWidth": LIFT_RIM_W},
        "shaft": {"unitHeight": SHAFT_UNIT, "topCollarDepth": SHAFT_TOP_DEPTH, "bandY": list(SHAFT_BAND_Y)},
        "catapult": {
            "backWallZ": BACK_Z, "ceilingY": TUN_H, "shaftBaseY": TUN_H + CEIL_T,
            "railStartZ": RAIL_START_Z, "tunnelEndZ": TUN_END_Z, "exitLightZ": EXIT_LIGHT_Z,
            "tunnelHalfWidth": TUN_HW, "tunnelChamfer": [TUN_KX, TUN_KY], "tunnelHeight": TUN_H,
            "exitFrameInset": EXIT_FRAME_IN,
            "cableAnchors": [list(a) for a in CABLE_ANCHORS],
            "railX": [-RAIL_X, RAIL_X], "grooveHalfWidth": GROOVE_HW, "grooveDepth": GROOVE_D,
            "liftGrooveStartZ": GROOVE_LIFT_Z0,
            "clamps": [[-RAIL_X, 0.0, CLAMP_Z], [RAIL_X, 0.0, CLAMP_Z]],
            "neonMaterial": "Neon",
            "neonRows": {"materials": NEON_ROW_NAMES, "z": [round(z, 4) for z in NEON_ROW_Z]},
            "lcd": {"materials": ["LcdLeft", "LcdRight"], "uPerMeterZ": [1.0 / LCD_TILE_M, -1.0 / LCD_TILE_M],
                    "tileMeters": LCD_TILE_M,
                    "textures": {"unable": "Resources/Models/Catapult/LcdLaunchUnable.dds",
                                 "ready": "Resources/Models/Catapult/LcdLaunchReady.dds"}},
        },
    }
    GEOMETRY_JSON.parent.mkdir(parents=True, exist_ok=True)
    GEOMETRY_JSON.write_text(json.dumps(data, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(f"{LOG} wrote {GEOMETRY_JSON}")


# ============================================================
# プレビュー
# ============================================================
SHAFT_PREVIEW_UNITS = 6


def place_preview(unit, top, clamp):
    """カタパルトを原点に、天井の上へ縦穴を積み、いちばん上に神殿の床の下の縁を置く。固定台は両脚の下"""
    clamp.location = Vector(ht.e2b((-RAIL_X, 0.0, CLAMP_Z)))
    other = bpy.data.objects.new("CatapultClamp_R", clamp.data)
    bpy.context.scene.collection.objects.link(other)
    other.location = Vector(ht.e2b((RAIL_X, 0.0, CLAMP_Z)))
    base = TUN_H + CEIL_T
    unit.location = Vector(ht.e2b((0.0, base, 0.0)))
    for k in range(1, SHAFT_PREVIEW_UNITS):
        obj = bpy.data.objects.new(f"StartShaftUnit_{k}", unit.data)
        bpy.context.scene.collection.objects.link(obj)
        obj.location = Vector(ht.e2b((0.0, base + SHAFT_UNIT * k, 0.0)))
    top.location = Vector(ht.e2b((0.0, base + SHAFT_UNIT * SHAFT_PREVIEW_UNITS + SHAFT_TOP_DEPTH, 0.0)))


def setup_preview_lighting():
    scene = bpy.context.scene
    world = bpy.data.worlds.new("Dark")
    scene.world = world
    world.use_nodes = True
    world.node_tree.nodes["Background"].inputs["Color"].default_value = (0.02, 0.02, 0.025, 1.0)
    lamps = [((0.0, 21.0, 0.0), 30000.0), ((0.0, 20.0, 40.0), 20000.0), ((0.0, 20.0, 90.0), 20000.0),
             ((0.0, 20.0, 140.0), 20000.0), ((0.0, 50.0, 0.0), 8000.0), ((0.0, 66.0, 0.0), 8000.0)]
    for k, (pos, energy) in enumerate(lamps):
        lamp = bpy.data.objects.new(f"Lamp{k}", bpy.data.lights.new(f"Lamp{k}", "POINT"))
        lamp.data.energy = energy
        lamp.data.color = (0.95, 0.97, 1.0)
        lamp.data.shadow_soft_size = 3.0
        lamp.location = Vector(ht.e2b(pos))
        scene.collection.objects.link(lamp)


# ============================================================
# IBL
# ============================================================
def set_launch_state(mats, tex, state):
    """ネオンの発光色と液晶の文字を、チェック未完了（unable）／完了（ready）にする"""
    for idx in [MAT_NEON] + list(range(MAT_NEON_ROW0, len(MAT_NAMES))):
        neon = mats[idx].node_tree.nodes["Principled BSDF"]
        neon.inputs["Emission Color"].default_value = (*NEON_EMISSION[state], 1.0)
    image = tex["LcdLaunchUnable" if state == "unable" else "LcdLaunchReady"]
    for idx in (MAT_LCD_L, MAT_LCD_R):
        for node in mats[idx].node_tree.nodes:
            if node.type == "TEX_IMAGE" and node.image is not None and node.image.name.startswith("LcdLaunch"):
                node.image = image


def render_ibl_raw(name):
    """IBL_EYE から正距円筒で撮り、線形の RGBA（縦×横×4 を平らにしたもの）を返す"""
    scene = bpy.context.scene
    ht.setup_render(*IBL_SIZE, IBL_SAMPLES)
    cam = scene.camera
    if cam is None:
        cam = bpy.data.objects.new("Camera", bpy.data.cameras.new("Camera"))
        scene.collection.objects.link(cam)
        scene.camera = cam
    cam.data.type = "PANO"
    cam.data.panorama_type = "EQUIRECTANGULAR"
    cam.data.clip_end = 400.0
    cam.location = Vector(ht.e2b(IBL_EYE))
    # 神殿の IBL（gen_hub_temple.py の render_ibl）・タイトルの空と同じ向き
    cam.rotation_euler = (math.radians(90.0), 0.0, 0.0)
    scene.view_settings.view_transform = "Standard"
    scene.view_settings.look = "None"
    scene.view_settings.exposure = 0.0
    tmp = Path(bpy.app.tempdir) / f"{name}_raw.exr"
    scene.render.image_settings.file_format = "OPEN_EXR"
    scene.render.image_settings.color_depth = "32"
    scene.render.filepath = str(tmp)
    bpy.ops.render.render(write_still=True)
    img = bpy.data.images.load(str(tmp))
    px = np.empty(img.size[0] * img.size[1] * 4, dtype=np.float32)
    img.pixels.foreach_get(px)
    return px.reshape(-1, 4), img.size


def save_hdr(px, size, path):
    out = bpy.data.images.new(path.stem, width=size[0], height=size[1], alpha=False, float_buffer=True)
    out.pixels.foreach_set(px.ravel())
    out.filepath_raw = str(path.resolve())
    out.file_format = "HDR"
    out.save()
    print(f"{LOG} wrote IBL {path}")


def render_ibl(path, mats, tex):
    """赤ピンク（path）と緑（<path>_ready.hdr）の 2 枚。倍率は赤ピンクの平均を IBL_MEAN にそろえる値で共通"""
    path.parent.mkdir(parents=True, exist_ok=True)
    weights = np.array([0.2126, 0.7152, 0.0722])
    scale = None
    for state, out in (("unable", path), ("ready", path.with_name(f"{path.stem}_ready{path.suffix}"))):
        set_launch_state(mats, tex, state)
        px, size = render_ibl_raw(out.stem)
        mean = float((px[:, :3] @ weights).mean())
        if scale is None:
            scale = IBL_MEAN / max(mean, 1e-6)
        px[:, :3] *= scale
        print(f"{LOG} IBL {state} mean {mean:.4f} x{scale:.3f} -> {mean * scale:.4f}")
        save_hdr(px, size, out)
    set_launch_state(mats, tex, "unable")


def main():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    export_dir = ht.arg_path(argv, "--export")
    preview_dir = ht.arg_path(argv, "--preview")
    save_path = ht.arg_path(argv, "--save")
    ibl_path = ht.arg_path(argv, "--ibl")

    bpy.ops.wm.read_factory_settings(use_empty=True)
    check_clearance()
    tex_dir = export_dir if export_dir is not None else Path(bpy.app.tempdir) / "catapult_tex"
    tex = make_textures(tex_dir)
    mats = make_materials(tex)
    lift = build_lift(mats)
    unit = build_shaft_unit(mats)
    top = build_shaft_top(mats)
    catapult = build_catapult(mats)
    exit_light = build_exit_light(mats)
    clamp = build_clamp(mats)
    lcd = build_lcd(mats)
    parts = [lift, unit, top, catapult, exit_light, clamp, lcd]
    ht.report_stats(parts)

    if export_dir is not None:
        ht.export_parts(export_dir, parts)
        patch_lift_floor(export_dir / "StartLift.gltf")
        write_geometry_json()

    place_preview(unit, top, clamp)
    if save_path is not None:
        save_path.parent.mkdir(parents=True, exist_ok=True)
        bpy.ops.wm.save_as_mainfile(filepath=str(save_path.resolve()))
    if preview_dir is None and ibl_path is None:
        return

    preview_floor_texture(mats)
    setup_preview_lighting()
    # IBL は機体を入れずに撮る（神殿と同じ）
    if ibl_path is not None:
        render_ibl(ibl_path, mats, tex)
    if preview_dir is None:
        return
    if "--with-mech" in argv:
        ht.import_mech()
    preview_dir.mkdir(parents=True, exist_ok=True)
    ht.setup_render(1280, 720, 48)
    scene = bpy.context.scene
    scene.view_settings.exposure = 0.0
    scene.render.image_settings.file_format = "PNG"
    scene.render.image_settings.color_depth = "8"
    eye = 12.5   # リフトの床から目まで（HUB のコクピットの目とほぼ同じ）
    views = {
        # 縦穴の中（リフトに乗った目の高さ。前を少し見上げる）
        "shaft": ((0.0, 55.0, 1.0), (0.0, 58.0, 12.0), 75.0),
        # 広間へ降りる途中（リフトが天井から 12m 下）
        "drop": ((0.0, TUN_H - 12.0 + eye, 1.0), (0.0, 8.0, 40.0), 75.0),
        # 射出口に寄った一人称（射出口の 30m 手前）と、斜め下から見上げた射出口
        "exit": ((0.0, 12.5, TUN_END_Z - 30.0), (0.0, 11.0, TUN_END_Z), 70.0),
        "exit_side": ((-9.0, 3.0, TUN_END_Z - 22.0), (2.0, 12.0, TUN_END_Z), 70.0),
        # 両脚の下の固定台とレール（前・上から）
        "clamp": ((6.0, 6.0, 9.0), (0.0, 0.0, 0.5), 60.0),
        # カタパルトに着いた一人称（射出路の先の射出口）
        "hall": ((0.0, eye, 1.0), (0.0, 11.0, 60.0), 70.0),
        # 三人称（機体の後方・左下）
        "tps": ((-8.0, 3.0, -11.0), (0.0, 13.0, 12.0), 75.0),
        # 縦穴を真上から（参考の図）
        "top": ((0.0, 95.0, 0.0), (0.0, 0.0, 0.001), 55.0),
    }
    only = argv[argv.index("--only") + 1].split(",") if "--only" in argv else list(views)
    for name in only:
        e, target, fov = views[name]
        ht.set_camera(e, target, fov)
        ht.render(preview_dir / f"catapult_{name}.png")


if __name__ == "__main__":
    main()
