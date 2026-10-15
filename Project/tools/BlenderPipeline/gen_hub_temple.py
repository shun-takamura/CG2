"""HUB の神殿（天球儀の聖堂）を Blender で手続き生成するスクリプト。17_HubScene.md §2 / H1・H2。

使い方（Project/ で実行）:
    blender -b --factory-startup -P tools/BlenderPipeline/gen_hub_temple.py -- [オプション]
        --export <フォルダ>   部位ごとに glTF とテクスチャ（床の模様も）を書き出す（例: Assets/Models/HubTemple）
        --ibl <パス.hdr>      機体の胸の位置から神殿を正距円筒で撮って IBL 用の .hdr にする（例: Assets/hub_temple_ibl.hdr）
        --preview <フォルダ>  確認用の PNG（待機の視点・コクピットから +Z・床から・見上げ）
        --with-mech           プレビューに HUB 用の機体を読み込む（遅い）
        --only <名前,...>     プレビューの構図を選ぶ（wait / gate / floor / back）
        --save <パス.blend>   生成した神殿を .blend で保存

方針（2026-10-14 ユーザー決定）: パルテナの神殿に寄せた虹色の神殿。本棚・回廊は置かない。
    +Z 側（ステージ選択の背景・降りるときに見える側）は「時計の門」（ペルソナ 3 リロードの入口が参考）。
    左右から弧を描いて上がる大階段の踊り場（高さ 10.6m）から、機体の手のすぐ手前（z 3.85）まで石の足場を伸ばす。

座標: 形はすべて**エンジンの座標**（Y 上・正面 +Z・機体の足元が原点）で組み、Blender へ入れるときに
(x, y, z) → (−x, −z, y) へ変える（cook が glTF の X を反転するのと合わせて、エンジンで同じ位置に戻る）。

部位（書き出すファイル）:
    HubTemple_Shell     … 壁・柱・窓・扉・垂れ幕・花・大階段と踊り場・足場・ドーム。原点＝エンジンの原点
    HubGate             … 時計の門の動かない部分（枠・文字の帯・文字盤・小さな時計・奥の光）。原点＝エンジンの原点
    HubGate_MinuteHand / HubGate_HourHand / HubGate_SmallHand / HubGear
                        … 門の動く部分。原点＝回る軸。**門の面は XY、Z 軸まわりに回す**（エンジンで rotate = (0, 0, 角度)）。
                          HubGear は半径 1 の歯車（エンジンで (r, r, 1) に拡大して使い回す）
    HubRing_A〜D        … 天球儀の輪。輪の面の法線はローカルの X。エンジンで rotate = (回転角, tiltY, tiltZ)
    HubFloor_BaseColor.png … 鏡の床（WaterSurface）の模様。原点がテクスチャの角に来る（floorTiling = 1 / FLOOR_TILE_M）
マテリアル: Marble / MarbleDark / Gold / Glass / Cloth / Rune / ClockFace / Step / Flowers / Light
（全部に法線マップを付ける＝PSO が submesh[0] で決まる罠の対策。平らなものは Gold_NormalMap を共有）
"""

import colorsys
import json
import math
import struct
import sys
from pathlib import Path

import bmesh
import bpy
import numpy as np
from mathutils import Matrix, Vector

# ============================================================
# 寸法（エンジン座標、m）
# ============================================================
TEMPLE_R = 22.0          # 壁の内側の半径（パンテオンの直径 43m とほぼ同じ）
WALL_T = 1.0
WALL_TOP = 22.4          # 壁の上端＝ドームの起点
CORNICE_Y = 21.4
BAYS = 16                # 壁の区画。区画の中心は 22.5° × i（i=0 が +Z＝門）、境目に柱
DOME_SEGS = 96

LANDING_Y = 10.6         # 踊り場（手のひらと同じ高さ）
LANDING_FRONT_R = 14.0   # 踊り場の前の縁（弧）
LANDING_HALF_DEG = 32.0  # 踊り場の広がり（±）。この外から大階段が下りる

STAIR_R0 = 15.5          # 大階段の内側・外側の半径
STAIR_R1 = 19.5
STAIR_SPAN_DEG = 95.0    # 踊り場の端から床まで回る角度
STAIR_RISE = 0.25

BRIDGE_END_Z = 3.85      # 足場の先。機体の手のいちばん前（z 3.69）から 15cm
BRIDGE_HALF_W = 1.2
BRIDGE_TOP = LANDING_Y + 0.005   # 踊り場と重なる所で z ファイティングしないよう、ほんの少し高く
BRIDGE_RAIL_START_Z = 7.0        # 手すりは待機の視点（z 6.6）より後ろから踊り場側だけ（前は開けて手のひらへ渡る）

COLUMN_R = 20.8          # 壁の前の溝付きの円柱
COLUMN_RADIUS = 0.65
GATE_ZONE_DEG = 34.0     # 門のまわり（±）には柱・窓を置かない

DOME_RIB_ELEV = (6.0, 17.0, 28.0, 39.0, 50.0, 62.0)
DOME_RIB_COLS = 28
OCULUS_R = 4.5

# 時計の門（+Z の壁の前。面は XY、−Z 側を向く）
GATE_CENTER = (0.0, 19.1, 19.8)
GATE_R_OUT = 8.5         # 枠の外の半径（下端が踊り場に着く）
GATE_R_FRAME = 7.3       # 枠の内の半径＝文字盤の外
GATE_R_HOLE = 4.2        # 文字盤の穴（奥の歯車と光が見える）
GATE_SLOT_HW = 2.6       # 下から中心へ抜ける通り道の半幅（タイトルの扉がここに立つ）
SMALL_CLOCKS = [(-3.4, 4.3), (3.4, 4.3), (-5.0, -1.7)]   # 門の面の (x, y)
SMALL_CLOCK_R = 1.9
# 門の動く部分（エンジンの HubTemple.cpp の表と同じ）。(モデル, 門の面の x, y, 奥行き, 大きさ, 度/秒, 初めの角度)
GATE_MOVERS = [
    ("HubGate_MinuteHand", 0.0, 0.0, -1.25, 1.0, 6.0, 40.0),
    ("HubGate_HourHand", 0.0, 0.0, -1.15, 1.0, 0.5, 300.0),
    ("HubGate_SmallHand", -3.4, 4.3, -1.12, 1.0, 12.0, 0.0),
    ("HubGate_SmallHand", 3.4, 4.3, -1.12, 1.0, -8.0, 120.0),
    ("HubGate_SmallHand", -5.0, -1.7, -1.12, 1.0, 18.0, 240.0),
    ("HubGear", 0.0, 0.0, 0.45, 3.2, 5.0, 0.0),
    ("HubGear", 3.15, -2.0, 0.25, 1.6, -10.0, 7.0),
    ("HubGear", -2.75, 1.85, 0.25, 1.45, -11.0, 3.0),
    ("HubGear", 1.9, 3.05, 0.1, 1.0, -16.0, 0.0),
    ("HubGear", -2.9, -2.5, 0.1, 1.2, -13.0, 11.0),
]

# 天球儀の輪（エンジン）。tiltY / tiltZ は度。spin は度/秒
RING_CENTER = (0.0, 12.0, 0.0)
RINGS = [
    # name,       半径, 中心,                 tiltY, tiltZ, spin, 輪の幅(半), 厚み(半), 惑星
    # 内側の 2 本は惑星を載せない（惑星の分だけ外の輪・足場に近づくため。回転はメダルと目盛りで見せる）
    ("HubRing_A", 11.0, RING_CENTER, 35.0, 90.0, 4.0, 0.28, 0.12, 0),
    ("HubRing_B", 12.2, RING_CENTER, -35.0, 120.0, -3.0, 0.28, 0.12, 0),
    ("HubRing_C", 13.4, RING_CENTER, 50.0, 90.0, 2.0, 0.32, 0.14, 2),
    ("HubRing_D", 12.0, (0.0, 2.0, 0.0), 0.0, 90.0, -1.5, 0.40, 0.16, 4),
]
# 輪の通ってはいけない所（足場・視点が通る範囲）と、機体からの最小距離
CORRIDOR_MIN = (-1.6, 9.0, 3.5)
CORRIDOR_MAX = (1.6, 14.5, 16.0)
RING_CLEARANCE = 1.0

# 床（WaterSurface の床テクスチャ。原点＝テクスチャの角、FLOOR_TILE_M で 1 周）
FLOOR_TILE_M = 44.0
FLOOR_TEX = 4096

# 質感
MAT_NAMES = ["Marble", "MarbleDark", "Gold", "Glass", "Cloth", "Rune", "ClockFace", "Step", "Flowers", "Light"]
(MAT_MARBLE, MAT_DARK, MAT_GOLD, MAT_GLASS, MAT_CLOTH, MAT_RUNE,
 MAT_CLOCK, MAT_STEP, MAT_FLOWERS, MAT_LIGHT) = range(len(MAT_NAMES))
UV_TILE_M = 2.0
TEX_SIZE = 1024
GOLD_SRGB = (1.0, 0.70, 0.30)
FLOWER_SWATCHES = 8      # Flowers テクスチャの色の数（横に並ぶ）
MECH_GLTF = Path("Assets/Models/HubMech/Export/HubMech.gltf")
IBL_EYE = (0.0, 12.0, 0.0)
IBL_SIZE = (1024, 512)
IBL_MEAN = 0.45          # IBL の平均の明るさ（線形）。撮った後にこの値へそろえる

LOG = "[gen_hub_temple]"


# ============================================================
# 座標
# ============================================================
def e2b(p):
    """エンジン → Blender"""
    return (-p[0], -p[2], p[1])


def polar(r, theta, y):
    """エンジンの極座標。theta は +Z から +X へ回る角（ラジアン）"""
    return (r * math.sin(theta), y, r * math.cos(theta))


def bay_frame(theta):
    """区画の向き。(中心への距離 d, 横 x, 高さ y) → エンジン"""
    s, c = math.sin(theta), math.cos(theta)

    def to_engine(d, x, y):
        return (d * s + x * c, y, d * c - x * s)
    return to_engine


P_E2B = np.array([[-1.0, 0.0, 0.0], [0.0, 0.0, -1.0], [0.0, 1.0, 0.0]])


def engine_rotation(rx, ry, rz):
    """エンジンの MakeAffineMatrix の回転（行ベクトルで Rx·Ry·Rz）を列ベクトルの 3x3 にする。度"""
    a, b, c = (math.radians(v) for v in (rx, ry, rz))
    mx = np.array([[1, 0, 0], [0, math.cos(a), math.sin(a)], [0, -math.sin(a), math.cos(a)]])
    my = np.array([[math.cos(b), 0, -math.sin(b)], [0, 1, 0], [math.sin(b), 0, math.cos(b)]])
    mz = np.array([[math.cos(c), math.sin(c), 0], [-math.sin(c), math.cos(c), 0], [0, 0, 1]])
    return (mx @ my @ mz).T


def gate_xform(center):
    """門の面の座標 (x, 奥行き, y) → エンジン。revolve（Y 軸まわり）で組んだ円が門の面（XY）に立つ。
    入力の 2 番目が奥行き（+ が壁の側）、3 番目が上"""
    cx, cy, cz = center
    return lambda p: (p[0] + cx, p[2] + cy, p[1] + cz)


def gp(x, y, d):
    """門の面の (x, y, 奥行き) を gate_xform の入力の並びにする"""
    return (x, d, y)


# ============================================================
# 形状
# ============================================================
class MeshBuilder:
    """頂点（エンジン座標。xform があれば先にかける）と面を溜めて最後に 1 オブジェクトにする。
    面の向きは閉じた立体ごとに外向きへそろえる。片面の板（facing を指定して作った面）はそろえる対象から外す。
    UV は頂点ごとに指定できる（指定の無い面はボックス投影）"""

    def __init__(self, xform=None):
        self.verts = []
        self.faces = []
        self.mats = []
        self.uvs = {}       # 面の番号 → {頂点の番号: UV}
        self.fixed = set()  # 向きを自分で決めた面
        self.xform = xform

    def _v(self, p):
        if self.xform is not None:
            p = self.xform(p)
        return e2b(p)

    def add_solid(self, verts, faces, mat, uv=None, vert_uvs=None):
        """uv: 全部の面に同じ UV（色見本から 1 色拾う）。vert_uvs: 頂点ごとの UV（None の頂点を含む面はボックス投影）"""
        base = len(self.verts)
        self.verts.extend(self._v(p) for p in verts)
        for f in faces:
            idx = len(self.faces)
            self.faces.append([base + i for i in f])
            self.mats.append(mat)
            if uv is not None:
                self.uvs[idx] = {base + i: uv for i in f}
            elif vert_uvs is not None and all(vert_uvs[i] is not None for i in f):
                self.uvs[idx] = {base + i: vert_uvs[i] for i in f}

    def poly_facing(self, verts, uvs, mat, facing):
        """片面の凸多角形。facing（入力の座標）の側へ表を向ける"""
        bv = [Vector(self._v(p)) for p in verts]
        normal = Vector((0.0, 0.0, 0.0))
        for i in range(len(bv)):
            normal += bv[i].cross(bv[(i + 1) % len(bv)])
        f = Vector(self._v(tuple(np.add(verts[0], facing)))) - bv[0]
        order = list(range(len(bv))) if normal.dot(f) > 0 else list(range(len(bv)))[::-1]
        base = len(self.verts)
        self.verts.extend(tuple(v) for v in bv)
        idx = len(self.faces)
        self.faces.append([base + i for i in order])
        self.mats.append(mat)
        if uvs is not None:
            self.uvs[idx] = {base + i: uvs[i] for i in range(len(bv))}
        self.fixed.add(idx)

    def grid_facing(self, pts, uvs, mat, facing):
        """頂点を共有する片面の格子（垂れ幕）。pts[i][j]（入力の座標）、uvs[i][j]"""
        rows, cols = len(pts), len(pts[0])
        base = len(self.verts)
        for row in pts:
            self.verts.extend(self._v(p) for p in row)
        for i in range(rows - 1):
            for j in range(cols - 1):
                ids = [(i, j), (i, j + 1), (i + 1, j + 1), (i + 1, j)]
                bv = [Vector(self.verts[base + a * cols + b]) for a, b in ids]
                n = (bv[1] - bv[0]).cross(bv[3] - bv[0])
                p0 = pts[ids[0][0]][ids[0][1]]
                f = Vector(self._v(tuple(np.add(p0, facing)))) - bv[0]
                if n.dot(f) < 0:
                    ids = ids[::-1]
                idx = len(self.faces)
                self.faces.append([base + a * cols + b for a, b in ids])
                self.mats.append(mat)
                self.uvs[idx] = {base + a * cols + b: uvs[a][b] for a, b in ids}
                self.fixed.add(idx)

    def hexa(self, c, mat, uv=None):
        """8 頂点（下 4 → 上 4、同じ回り順）の六面体"""
        f = [(0, 3, 2, 1), (4, 5, 6, 7), (0, 1, 5, 4), (1, 2, 6, 5), (2, 3, 7, 6), (3, 0, 4, 7)]
        self.add_solid(c, f, mat, uv)

    def box(self, center, half, yaw, mat, uv=None):
        """Y 軸まわりに yaw（ラジアン）回した箱。ローカルの +Z が極座標の yaw の向き"""
        cx, cy, cz = center
        hx, hy, hz = half
        s, co = math.sin(yaw), math.cos(yaw)
        pts = []
        for y in (-hy, hy):
            for x, z in ((-hx, -hz), (hx, -hz), (hx, hz), (-hx, hz)):
                pts.append((cx + x * co + z * s, cy + y, cz - x * s + z * co))
        self.hexa(pts, mat, uv)

    def radial_box(self, theta, r0, r1, y0, y1, half_w, mat, center=(0.0, 0.0)):
        rc = (r0 + r1) * 0.5
        self.box((center[0] + rc * math.sin(theta), (y0 + y1) * 0.5, center[1] + rc * math.cos(theta)),
                 (half_w, (y1 - y0) * 0.5, (r1 - r0) * 0.5), theta, mat)

    def revolve(self, profile, segs, mat, th0=0.0, th1=2.0 * math.pi, center=(0.0, 0.0), uv=None):
        """(r, y) の閉じた多角形を Y 軸（center の上）まわりに回す。全周でなければ両端にふた"""
        full = abs((th1 - th0) - 2.0 * math.pi) < 1e-6
        n = len(profile)
        cols = segs if full else segs + 1
        verts = []
        for i in range(cols):
            t = th0 + (th1 - th0) * i / segs
            for r, y in profile:
                verts.append((center[0] + r * math.sin(t), y, center[1] + r * math.cos(t)))
        faces = []
        for i in range(segs):
            i2 = (i + 1) % cols
            for k in range(n):
                k2 = (k + 1) % n
                faces.append((i * n + k, i * n + k2, i2 * n + k2, i2 * n + k))
        if not full:
            faces.append(list(range(n)))
            faces.append(list(range((cols - 1) * n, cols * n))[::-1])
        self.add_solid(verts, faces, mat, uv)

    def ring_rect(self, r0, r1, y0, y1, segs, mat, th0=0.0, th1=2.0 * math.pi, center=(0.0, 0.0), uv=None):
        self.revolve([(r0, y0), (r1, y0), (r1, y1), (r0, y1)], segs, mat, th0, th1, center, uv)

    def lathe(self, cx, cz, radius_fn, ys, segs, mat):
        """高さごとに半径の関数 radius_fn(y, φ) で回した柱（溝付きの円柱など）。上下にふた"""
        verts = []
        for y in ys:
            for i in range(segs):
                t = 2.0 * math.pi * i / segs
                r = radius_fn(y, t)
                verts.append((cx + r * math.sin(t), y, cz + r * math.cos(t)))
        n = len(ys)
        faces = [list(range(segs)), list(range((n - 1) * segs, n * segs))[::-1]]
        for k in range(n - 1):
            for i in range(segs):
                j = (i + 1) % segs
                faces.append((k * segs + i, k * segs + j, (k + 1) * segs + j, (k + 1) * segs + i))
        self.add_solid(verts, faces, mat)

    def cylinder(self, cx, cz, radius, y0, y1, segs, mat, uv=None):
        verts = []
        for y in (y0, y1):
            for i in range(segs):
                t = 2.0 * math.pi * i / segs
                verts.append((cx + radius * math.sin(t), y, cz + radius * math.cos(t)))
        faces = [list(range(segs)), list(range(segs, 2 * segs))[::-1]]
        for i in range(segs):
            j = (i + 1) % segs
            faces.append((i, j, segs + j, segs + i))
        self.add_solid(verts, faces, mat, uv)

    def sphere(self, center, radius, mat, segs=12, rings=8, uv=None, scale_y=1.0):
        verts = [(center[0], center[1] - radius * scale_y, center[2])]
        for k in range(1, rings):
            phi = -math.pi * 0.5 + math.pi * k / rings
            for i in range(segs):
                t = 2.0 * math.pi * i / segs
                verts.append((center[0] + radius * math.cos(phi) * math.sin(t),
                              center[1] + radius * math.sin(phi) * scale_y,
                              center[2] + radius * math.cos(phi) * math.cos(t)))
        verts.append((center[0], center[1] + radius * scale_y, center[2]))
        top = len(verts) - 1
        faces = []
        for i in range(segs):
            faces.append((0, 1 + (i + 1) % segs, 1 + i))
        for k in range(rings - 2):
            a = 1 + k * segs
            b = a + segs
            for i in range(segs):
                j = (i + 1) % segs
                faces.append((a + i, a + j, b + j, b + i))
        last = 1 + (rings - 2) * segs
        for i in range(segs):
            faces.append((last + i, last + (i + 1) % segs, top))
        self.add_solid(verts, faces, mat, uv)

    def tube(self, path, half, mat, up_axis=(0.0, 1.0, 0.0)):
        """折れ線に沿った四角い断面の棒。up_axis は断面の向きを決める軸（道筋と平行にならないもの）"""
        verts = []
        n = len(path)
        for i, p in enumerate(path):
            t = np.subtract(path[min(i + 1, n - 1)], path[max(i - 1, 0)])
            t = t / np.linalg.norm(t)
            side = np.cross(t, up_axis)
            side = side / np.linalg.norm(side)
            up = np.cross(side, t)
            for sx, sy in ((-1, -1), (1, -1), (1, 1), (-1, 1)):
                verts.append(tuple(np.asarray(p) + side * half * sx + up * half * sy))
        faces = [(0, 1, 2, 3), tuple(range((n - 1) * 4, n * 4))[::-1]]
        for i in range(n - 1):
            a, b = i * 4, (i + 1) * 4
            for k in range(4):
                k2 = (k + 1) % 4
                faces.append((a + k, a + k2, b + k2, b + k))
        self.add_solid(verts, faces, mat)

    def band2d(self, inner, outer, to3d, d0, d1, mat):
        """同じ点数の 2 本の線（2D）の間を埋めて奥行き d0..d1 の板にする（アーチの枠）。to3d(d, x, y)"""
        n = len(inner)
        verts = ([to3d(d0, x, y) for x, y in inner] + [to3d(d0, x, y) for x, y in outer]
                 + [to3d(d1, x, y) for x, y in inner] + [to3d(d1, x, y) for x, y in outer])
        fi, fo, bi, bo = 0, n, 2 * n, 3 * n
        faces = []
        for i in range(n - 1):
            j = i + 1
            faces.append((fi + i, fi + j, fo + j, fo + i))
            faces.append((bi + i, bo + i, bo + j, bi + j))
            faces.append((fi + i, bi + i, bi + j, fi + j))
            faces.append((fo + i, fo + j, bo + j, bo + i))
        for k in (0, n - 1):
            faces.append((fi + k, fo + k, bo + k, bi + k))
        self.add_solid(verts, faces, mat)

    def build(self, name, materials):
        mesh = bpy.data.meshes.new(name)
        mesh.from_pydata(self.verts, [], self.faces)
        for poly, m in zip(mesh.polygons, self.mats):
            poly.material_index = m
        mesh.update()
        bm = bmesh.new()
        bm.from_mesh(mesh)
        bm.faces.ensure_lookup_table()
        free = [f for f in bm.faces if f.index not in self.fixed]
        bmesh.ops.recalc_face_normals(bm, faces=free)
        uv = bm.loops.layers.uv.verify()
        for face in bm.faces:
            given = self.uvs.get(face.index)
            if given is not None:
                for loop in face.loops:
                    loop[uv].uv = given[loop.vert.index]
                continue
            # ボックス投影（大理石は継ぎ目の無いタイルなので面の境目は模様に紛れる）
            nrm = face.normal
            ax = max(range(3), key=lambda i: abs(nrm[i]))
            u_axis, v_axis = [(1, 2), (0, 2), (0, 1)][ax]
            for loop in face.loops:
                co = loop.vert.co
                loop[uv].uv = (co[u_axis] / UV_TILE_M, co[v_axis] / UV_TILE_M)
        bm.to_mesh(mesh)
        bm.free()
        for poly in mesh.polygons:
            poly.use_smooth = True
        mesh.set_sharp_from_angle(angle=math.radians(35.0))
        obj = bpy.data.objects.new(name, mesh)
        for mat in materials:
            obj.data.materials.append(mat)
        bpy.context.scene.collection.objects.link(obj)
        return obj


def flower_uv(k):
    return ((k + 0.5) / FLOWER_SWATCHES, 0.5)


# ============================================================
# 神殿の殻
# ============================================================
def bay_boundary(i):
    return math.radians(22.5 * i + 11.25)


def in_gate_zone(theta):
    a = (math.degrees(theta) + 180.0) % 360.0 - 180.0
    return abs(a) < GATE_ZONE_DEG


def build_walls(mb):
    mb.ring_rect(TEMPLE_R, TEMPLE_R + WALL_T, 0.0, WALL_TOP, DOME_SEGS, MAT_MARBLE)
    mb.ring_rect(TEMPLE_R - 0.25, TEMPLE_R + 0.01, 0.0, 0.5, DOME_SEGS, MAT_DARK)
    mb.ring_rect(TEMPLE_R - 0.8, TEMPLE_R + 0.01, CORNICE_Y, WALL_TOP, DOME_SEGS, MAT_MARBLE)
    mb.ring_rect(TEMPLE_R - 0.86, TEMPLE_R - 0.78, CORNICE_Y + 0.3, CORNICE_Y + 0.55, DOME_SEGS, MAT_GOLD)
    mb.ring_rect(TEMPLE_R - 0.5, TEMPLE_R + 0.01, 10.9, 11.3, DOME_SEGS, MAT_MARBLE)   # 上下の段の境の帯
    mb.ring_rect(TEMPLE_R - 0.55, TEMPLE_R - 0.48, 11.0, 11.15, DOME_SEGS, MAT_GOLD)


def fluted_column(mb, cx, cz, radius, y0, y1):
    """パルテナの柱：溝の入った柱身・金の帯・金の柱頭・濃い色の台座"""
    flutes = 20
    mb.lathe(cx, cz, lambda y, t: radius * (1.0 - 0.07 * abs(math.sin(flutes * t * 0.5))), [y0, y1],
             flutes * 4, MAT_MARBLE)
    mb.revolve([(0.0001, 0.0), (radius + 0.35, 0.0), (radius + 0.35, 0.5), (radius + 0.2, 0.65),
                (radius + 0.15, y0), (0.0001, y0)], 32, MAT_DARK, center=(cx, cz))
    mb.revolve([(radius - 0.02, y0), (radius + 0.12, y0), (radius + 0.12, y0 + 0.15), (radius - 0.02, y0 + 0.15)],
               32, MAT_GOLD, center=(cx, cz))
    for yb in (y0 + (y1 - y0) * 0.33, y0 + (y1 - y0) * 0.66):
        mb.ring_rect(radius - 0.05, radius + 0.08, yb, yb + 0.25, 32, MAT_GOLD, center=(cx, cz))
    mb.revolve([(0.0001, y1), (radius + 0.02, y1), (radius + 0.45, y1 + 0.6), (radius + 0.5, y1 + 0.8),
                (0.0001, y1 + 0.8)], 32, MAT_GOLD, center=(cx, cz))


def sconce(mb, theta, r_col, y):
    """柱の内側（中心の向き）の燭台：腕・金の皿・炎（Light）・下に提げた金の輪"""
    r_arm0 = r_col - COLUMN_RADIUS + 0.05
    r_arm1 = r_arm0 - 0.7
    mb.radial_box(theta, r_arm1, r_arm0, y - 0.06, y + 0.06, 0.07, MAT_GOLD)
    cx, _, cz = polar(r_arm1 - 0.05, theta, 0.0)
    mb.revolve([(0.0001, y - 0.15), (0.12, y - 0.15), (0.32, y + 0.12), (0.28, y + 0.16), (0.0001, y + 0.05)],
               16, MAT_GOLD, center=(cx, cz))
    mb.sphere((cx, y + 0.32, cz), 0.17, MAT_LIGHT, 10, 6, scale_y=1.6)
    # 提げた輪（中心を向く面に立つ）
    ring_c = polar(r_arm0 - 0.35, theta, y - 0.55)
    radial = np.array([math.sin(theta), 0.0, math.cos(theta)])
    tangent = np.array([math.cos(theta), 0.0, -math.sin(theta)])
    pts = []
    for k in range(25):
        a = 2.0 * math.pi * k / 24
        pts.append(tuple(np.asarray(ring_c) + tangent * 0.32 * math.cos(a) + np.array([0.0, 0.32, 0.0]) * math.sin(a)))
    mb.tube(pts, 0.035, MAT_GOLD, up_axis=tuple(radial))


def pointed_arch(hw, y0, ys, rise, n=12):
    """尖頭アーチの開口部の輪郭（左下 → 頂点 → 右下）"""
    c = (rise * rise - hw * hw) / (2.0 * hw)
    r = hw + c
    apex_ang = math.atan2(rise, -c)
    left = [(-hw, y0)]
    for k in range(n + 1):
        a = math.pi + (apex_ang - math.pi) * k / n
        left.append((c + r * math.cos(a), ys + r * math.sin(a)))
    right = [(-x, y) for x, y in left[::-1]]
    return left + right[1:]


def arch_window(mb, theta, d_glass, hw, y0, ys, rise, frame_w, mullions=0):
    to3d = bay_frame(theta)
    inner = pointed_arch(hw, y0, ys, rise)
    outer = pointed_arch(hw + frame_w, y0, ys, rise + frame_w * 1.3)
    mb.band2d(inner, outer, to3d, d_glass - 0.35, d_glass + 0.05, MAT_GOLD)
    top = ys + rise
    uvs = [((x + hw) / (2.0 * hw), (y - y0) / (top - y0)) for x, y in inner]
    mb.poly_facing([to3d(d_glass, x, y) for x, y in inner], uvs, MAT_GLASS, polar(-1.0, theta, 0.0))
    # 縦の桟と横の桟
    for k in range(mullions):
        x = -hw + 2.0 * hw * (k + 1) / (mullions + 1)
        pts = [to3d(d, x + dx, y) for y in (y0, ys) for d, dx in ((d_glass - 0.15, -0.08), (d_glass - 0.15, 0.08),
                                                                  (d_glass, 0.08), (d_glass, -0.08))]
        mb.hexa(pts, MAT_GOLD)
    if mullions:
        yt = y0 + (ys - y0) * 0.55
        pts = [to3d(d, x, y) for y in (yt - 0.08, yt + 0.08) for d, x in ((d_glass - 0.15, -hw), (d_glass - 0.15, hw),
                                                                         (d_glass, hw), (d_glass, -hw))]
        mb.hexa(pts, MAT_GOLD)


def arch_door(mb, theta, d, hw, y0, ys, rise):
    """パルテナの尖頭アーチの金の扉（飾りの帯・合わせ目・輪の取っ手）"""
    to3d = bay_frame(theta)
    inner = pointed_arch(hw, y0, ys, rise)
    outer = pointed_arch(hw + 0.45, y0, ys, rise + 0.6)
    mb.band2d(inner, outer, to3d, d - 0.45, d + 0.05, MAT_MARBLE)
    mid = pointed_arch(hw + 0.12, y0, ys, rise + 0.16)
    mb.band2d(inner, mid, to3d, d - 0.5, d - 0.4, MAT_GOLD)
    mb.poly_facing([to3d(d, x, y) for x, y in inner], None, MAT_GOLD, polar(-1.0, theta, 0.0))
    # 合わせ目（濃い線）と横の帯
    mb.hexa([to3d(dd, dx, y) for y in (y0, ys + rise - 0.05) for dd, dx in ((d - 0.06, -0.04), (d - 0.06, 0.04),
                                                                            (d, 0.04), (d, -0.04))], MAT_DARK)
    for yb in (y0 + 1.0, y0 + (ys - y0) * 0.55):
        mb.hexa([to3d(dd, x, y) for y in (yb, yb + 0.22) for dd, x in ((d - 0.1, -hw), (d - 0.1, hw),
                                                                      (d, hw), (d, -hw))], MAT_GOLD)
    # 取っ手の輪
    radial = polar(1.0, theta, 0.0)
    for sx in (-0.45, 0.45):
        c = np.asarray(to3d(d - 0.12, sx, y0 + (ys - y0) * 0.45))
        tangent = np.subtract(to3d(0.0, 1.0, 0.0), to3d(0.0, 0.0, 0.0))
        pts = [tuple(c + tangent * 0.3 * math.cos(2.0 * math.pi * k / 20) + np.array([0, 0.3, 0]) * math.sin(2.0 * math.pi * k / 20))
               for k in range(21)]
        mb.tube(pts, 0.04, MAT_GOLD, up_axis=radial)


def curtain(mb, theta, d, x_outer, x_inner, y_top, y_bottom, y_tie):
    """窓の脇の垂れ幕。上は広く、留め紐で細く、下でまた少し広がる。ひだは奥行きの波"""
    to3d = bay_frame(theta)
    rows, cols = 18, 14
    pts, uvs = [], []
    for i in range(rows):
        v = i / (rows - 1)
        y = y_top + (y_bottom - y_top) * v
        if y >= y_tie:
            g = 1.0 - 0.6 * ((y_top - y) / (y_top - y_tie)) ** 1.5
        else:
            g = 0.4 + 0.35 * ((y_tie - y) / (y_tie - y_bottom))
        row, urow = [], []
        for j in range(cols):
            u = j / (cols - 1)
            x = x_outer + (x_inner - x_outer) * u * g
            depth = d - 0.12 - 0.12 * math.sin(u * math.pi * 5.0) * (0.5 + 0.5 * g)
            row.append(to3d(depth, x, y))
            urow.append((u, 1.0 - v))
        pts.append(row)
        uvs.append(urow)
    mb.grid_facing(pts, uvs, MAT_CLOTH, polar(-1.0, theta, 0.0))
    # 留め紐（金）
    xt = x_outer + (x_inner - x_outer) * 0.4
    a, b = sorted((x_outer, xt))
    mb.hexa([to3d(dd, x, y) for y in (y_tie - 0.1, y_tie + 0.1) for dd, x in ((d - 0.35, a), (d - 0.35, b),
                                                                             (d - 0.05, b), (d - 0.05, a))], MAT_GOLD)


def planter(mb, pos, rng):
    """鉢と花（青・白・水色・黄の花と葉。色は Flowers テクスチャの色見本）"""
    x, y, z = pos
    mb.revolve([(0.0001, y), (0.45, y), (0.5, y + 0.1), (0.3, y + 0.3), (0.38, y + 0.8), (0.7, y + 1.05),
                (0.75, y + 1.2), (0.0001, y + 1.2)], 20, MAT_DARK, center=(x, z))
    mb.ring_rect(0.72, 0.8, y + 1.12, y + 1.25, 20, MAT_GOLD, center=(x, z))
    for k in range(40):
        a = rng.uniform(0.0, 2.0 * math.pi)
        r = 0.75 * math.sqrt(rng.uniform(0.0, 1.0))
        h = y + 1.25 + rng.uniform(0.0, 0.45) * (1.0 - r / 0.9)
        leaf = k % 4 == 0
        color = 6 if leaf else int(rng.choice([0, 0, 1, 1, 2, 3, 4]))
        mb.sphere((x + r * math.cos(a), h, z + r * math.sin(a)), rng.uniform(0.12, 0.2) if not leaf else 0.22,
                  MAT_FLOWERS, 8, 5, uv=flower_uv(color))


def build_bays(mb, rng):
    for i in range(BAYS):
        th = math.radians(22.5 * i)
        if in_gate_zone(th):
            continue
        deg = (22.5 * i) % 360.0
        if i == 8:
            # 機体の後ろ（−Z）：いちばん大きなとがったアーチの窓（待機の視点の見せ場）
            arch_window(mb, th, 21.25, 3.2, 0.6, 15.0, 5.2, 0.4, mullions=2)
            curtain(mb, th, 21.25, -3.55, -2.4, 21.0, 0.8, 9.0)
            curtain(mb, th, 21.25, 3.55, 2.4, 21.0, 0.8, 9.0)
            continue
        # 上の段：虹色のとがったアーチの窓と垂れ幕
        arch_window(mb, th, 21.75, 2.2, 11.8, 18.2, 2.2, 0.3, mullions=1)
        curtain(mb, th, 21.55, -3.2, -2.0, 21.2, 11.6, 15.5)
        curtain(mb, th, 21.55, 3.2, 2.0, 21.2, 11.6, 15.5)
        # 1 階（大階段の無い奥の区画だけ）：金の扉と花の鉢
        if 130.0 <= deg <= 230.0:
            arch_door(mb, th, 21.75, 1.8, 0.4, 5.6, 1.8)
            to3d = bay_frame(th)
            for sx in (-2.3, 2.3):   # 柱の台座に当たらないよう少し前へ
                planter(mb, to3d(19.3, sx, 0.0), rng)


def build_columns(mb):
    for i in range(BAYS):
        th = bay_boundary(i)
        if in_gate_zone(th):
            continue
        x, _, z = polar(COLUMN_R, th, 0.0)
        fluted_column(mb, x, z, COLUMN_RADIUS, 0.8, CORNICE_Y - 0.8)
        # 大階段の外側をかすめるので、階段のいちばん高い所（柱の位置で 7.9m）より上に付ける
        sconce(mb, th, COLUMN_R, 9.0)


def build_landing_and_stairs(mb, rng):
    th_l = math.radians(LANDING_HALF_DEG)
    # 踊り場の下の台座（前の縁は弧）
    mb.ring_rect(LANDING_FRONT_R, TEMPLE_R + 0.05, 0.0, LANDING_Y, 64, MAT_MARBLE, -th_l, th_l)
    mb.ring_rect(LANDING_FRONT_R - 0.12, LANDING_FRONT_R + 0.02, LANDING_Y - 0.5, LANDING_Y - 0.25, 64, MAT_GOLD,
                 -th_l, th_l)
    mb.ring_rect(LANDING_FRONT_R - 0.1, LANDING_FRONT_R + 0.02, 0.0, 0.5, 64, MAT_DARK, -th_l, th_l)
    for deg in (-26.0, -15.0, 15.0, 26.0):
        x, _, z = polar(LANDING_FRONT_R - 0.1, math.radians(deg), 0.0)
        fluted_column(mb, x, z, 0.45, 0.5, LANDING_Y - 1.4)
    # 踊り場の手すり（足場の所と階段の降り口は開ける）
    gap = math.asin((BRIDGE_HALF_W + 0.2) / (LANDING_FRONT_R + 0.3))
    for sgn in (-1.0, 1.0):
        a0, a1 = sorted((sgn * gap, sgn * (th_l - math.radians(1.5))))
        r = LANDING_FRONT_R + 0.3
        mb.ring_rect(r - 0.18, r + 0.18, LANDING_Y, LANDING_Y + 0.15, 24, MAT_MARBLE, a0, a1)
        mb.ring_rect(r - 0.12, r + 0.12, LANDING_Y + 0.95, LANDING_Y + 1.05, 24, MAT_MARBLE, a0, a1)
        mb.ring_rect(r - 0.08, r + 0.08, LANDING_Y + 1.05, LANDING_Y + 1.12, 24, MAT_GOLD, a0, a1)
        count = int((a1 - a0) * r / 0.45)
        for k in range(count + 1):
            x, _, z = polar(r, a0 + (a1 - a0) * k / count, 0.0)
            mb.cylinder(x, z, 0.07, LANDING_Y + 0.15, LANDING_Y + 0.95, 8, MAT_MARBLE)
    # 踊り場の花
    for sx in (-4.2, 4.2):
        planter(mb, (sx, LANDING_Y, 17.6), rng)

    # 大階段（左右から弧を描いて踊り場へ）。段の上面は虹色（Step の色見本を段ごとに拾う）
    steps = int(round(LANDING_Y / STAIR_RISE))
    rise = LANDING_Y / steps
    span = math.radians(STAIR_SPAN_DEG)
    da = span / (steps - 1)
    for sgn in (-1.0, 1.0):
        rail = []
        for k in range(steps - 1):
            top = LANDING_Y - rise * (k + 1)
            a0 = th_l + da * k
            a1 = a0 + da
            lo, hi = sorted((sgn * a0, sgn * a1))
            mb.ring_rect(STAIR_R0, STAIR_R1, 0.0, top - 0.08, 3, MAT_MARBLE, lo, hi)
            hue = (k / (steps - 1)) * 0.9
            mb.ring_rect(STAIR_R0 - 0.02, STAIR_R1 + 0.02, top - 0.08, top, 3, MAT_STEP, lo, hi, uv=(hue, 0.5))
            # 土台の内側の面に段に沿った金の線（床から見上げたとき白い壁だけにならないように）
            mb.ring_rect(STAIR_R0 - 0.05, STAIR_R0 + 0.01, top - 0.45, top - 0.3, 3, MAT_GOLD, lo, hi)
            if top > 1.2:
                mb.ring_rect(STAIR_R0 - 0.04, STAIR_R0 + 0.01, 0.0, 0.5, 3, MAT_DARK, lo, hi)
            if k % 3 == 0:
                x, _, z = polar(STAIR_R0 + 0.15, sgn * (a0 + da * 0.5), 0.0)
                mb.cylinder(x, z, 0.06, top, top + 0.95, 8, MAT_GOLD)
            rail.append(polar(STAIR_R0 + 0.15, sgn * (a0 + da * 0.5), top + 0.95))
        mb.tube(rail, 0.05, MAT_GOLD)
        # 降り口の花
        planter(mb, polar(STAIR_R0 - 1.0, sgn * (th_l + span + math.radians(2.0)), 0.0), rng)


def build_bridge(mb):
    """踊り場の縁から機体の手のすぐ手前へ伸ばす石の足場（持ち送りで支える）"""
    z_end = BRIDGE_END_Z
    z_root = LANDING_FRONT_R + 0.05
    w = BRIDGE_HALF_W
    y = BRIDGE_TOP
    mb.box((0.0, y - 0.3, (z_end + z_root) * 0.5), (w, 0.3, (z_root - z_end) * 0.5), 0.0, MAT_MARBLE)
    mb.box((0.0, y - 0.3, z_end - 0.02), (w + 0.02, 0.12, 0.03), 0.0, MAT_GOLD)
    for sx in (-1.0, 1.0):
        mb.box((sx * (w + 0.01), y - 0.42, (z_end + z_root) * 0.5), (0.03, 0.06, (z_root - z_end) * 0.5), 0.0, MAT_GOLD)
    zc = 7.5
    mb.hexa([(-w * 0.8, y - 0.6, zc), (w * 0.8, y - 0.6, zc), (w * 0.8, y - 0.6, z_root), (-w * 0.8, y - 0.6, z_root),
             (-w * 0.8, y - 0.6, zc), (w * 0.8, y - 0.6, zc), (w * 0.8, y - 3.6, z_root), (-w * 0.8, y - 3.6, z_root)],
            MAT_MARBLE)
    z0 = BRIDGE_RAIL_START_Z
    for sx in (-1.0, 1.0):
        x = sx * (w - 0.12)
        mb.box((x, y + 0.075, (z0 + z_root) * 0.5), (0.15, 0.075, (z_root - z0) * 0.5), 0.0, MAT_MARBLE)
        mb.box((x, y + 1.0, (z0 + z_root) * 0.5), (0.11, 0.05, (z_root - z0) * 0.5), 0.0, MAT_MARBLE)
        mb.box((x, y + 1.085, (z0 + z_root) * 0.5), (0.07, 0.035, (z_root - z0) * 0.5), 0.0, MAT_GOLD)
        mb.box((x, y + 0.6, z0), (0.16, 0.6, 0.16), 0.0, MAT_MARBLE)   # 親柱
        mb.sphere((x, y + 1.32, z0), 0.14, MAT_GOLD)
        n = int((z_root - z0) / 0.45)
        for k in range(1, n + 1):
            mb.cylinder(x, z0 + (z_root - z0) * k / n, 0.06, y + 0.15, y + 0.95, 8, MAT_MARBLE)


def build_gate_pillars(mb):
    """門の根元の両脇：金の格子の柱・渦巻きの飾り"""
    zc = 18.6
    for sx in (-1.0, 1.0):
        x = sx * 9.8
        y0, y1 = LANDING_Y, LANDING_Y + 5.8
        mb.box((x, (y0 + y1) * 0.5, zc), (0.8, (y1 - y0) * 0.5, 0.6), 0.0, MAT_DARK)
        mb.box((x, y1 + 0.2, zc), (0.95, 0.2, 0.72), 0.0, MAT_GOLD)
        mb.box((x, y0 + 0.2, zc), (0.95, 0.2, 0.72), 0.0, MAT_GOLD)
        mb.box((x, y1 + 0.55, zc), (0.5, 0.15, 0.4), 0.0, MAT_MARBLE)
        mb.sphere((x, y1 + 1.05, zc), 0.38, MAT_GOLD, 16, 10)
        # 前の面の格子（縦横の金の棒）
        zf = zc - 0.62
        for k in range(5):
            xx = x - 0.6 + 1.2 * k / 4
            mb.box((xx, (y0 + y1) * 0.5, zf), (0.035, (y1 - y0) * 0.5 - 0.4, 0.03), 0.0, MAT_GOLD)
        for k in range(13):
            yy = y0 + 0.5 + (y1 - y0 - 1.0) * k / 12
            mb.box((x, yy, zf), (0.6, 0.035, 0.03), 0.0, MAT_GOLD)
        # 渦巻き（門の側の根元）
        cx, cy = x - sx * 1.4, y0 + 1.1
        pts = []
        for k in range(60):
            t = 3.2 * math.pi * k / 59
            r = 0.9 * (1.0 - t / (3.6 * math.pi))
            pts.append((cx + sx * r * math.cos(t + math.pi * 0.5), cy + r * math.sin(t + math.pi * 0.5), zc - 0.4))
        mb.tube(pts, 0.09, MAT_GOLD, up_axis=(0.0, 0.0, 1.0))


def build_shell(mats, rng):
    mb = MeshBuilder()
    build_walls(mb)
    build_columns(mb)
    build_bays(mb, rng)
    build_landing_and_stairs(mb, rng)
    build_bridge(mb)
    build_gate_pillars(mb)
    build_dome(mb)
    return mb.build("HubTemple_Shell", mats)


def build_dome(mb):
    rd = TEMPLE_R
    e_top = math.acos(OCULUS_R / rd)
    e_top_out = math.acos(OCULUS_R / (rd + WALL_T))
    inner = [(rd * math.cos(e), WALL_TOP + rd * math.sin(e)) for e in np.linspace(0.0, e_top, 28)]
    outer = [((rd + WALL_T) * math.cos(e), WALL_TOP + (rd + WALL_T) * math.sin(e)) for e in np.linspace(e_top_out, 0.0, 28)]
    mb.revolve(inner + outer, DOME_SEGS, MAT_MARBLE)

    def on_sphere(r, e_deg):
        e = math.radians(e_deg)
        return (r * math.cos(e), WALL_TOP + r * math.sin(e))

    for e in DOME_RIB_ELEV:
        h = math.degrees(0.28 / rd)
        mb.revolve([on_sphere(rd + 0.02, e - h), on_sphere(rd + 0.02, e + h),
                    on_sphere(rd - 0.5, e + h), on_sphere(rd - 0.5, e - h)], DOME_SEGS, MAT_MARBLE)
    e0, e1 = DOME_RIB_ELEV[0], DOME_RIB_ELEV[-1]
    arc_in = [on_sphere(rd - 0.5, e) for e in np.linspace(e0, e1, 16)]
    arc_out = [on_sphere(rd + 0.02, e) for e in np.linspace(e1, e0, 16)]
    half = 0.25 / (rd * math.cos(math.radians(30.0)))
    for k in range(DOME_RIB_COLS):
        t = 2.0 * math.pi * k / DOME_RIB_COLS
        mb.revolve(arc_in + arc_out, 1, MAT_MARBLE, t - half, t + half)
    for a, b in zip(DOME_RIB_ELEV[:-1], DOME_RIB_ELEV[1:]):
        em = math.radians((a + b) * 0.5)
        for k in range(DOME_RIB_COLS):
            t = 2.0 * math.pi * (k + 0.5) / DOME_RIB_COLS
            r = (rd - 0.05) * math.cos(em)
            size = 0.32 * (1.0 - 0.4 * (em / math.radians(62.0)))
            mb.sphere((r * math.sin(t), WALL_TOP + (rd - 0.05) * math.sin(em), r * math.cos(t)), size, MAT_GOLD, 10, 6)
    y_in = WALL_TOP + rd * math.sin(e_top)
    mb.ring_rect(OCULUS_R - 0.2, OCULUS_R + 0.02, y_in - 0.4, y_in + 1.2, 64, MAT_GOLD)


# ============================================================
# 時計の門
# ============================================================
def slotted_annulus(mb, r_in, r_out, d0, d1, slot_hw, mat, segs, uv_fn=None):
    """門の面の輪（奥行き d0..d1 の板）。slot_hw > 0 なら下から中心へ抜ける通り道で切る（切り口は直線）。
    uv_fn(x, y, r, s) は表（d0 の面）の UV。s は輪に沿った 0..1"""
    def angle_range(r):
        if slot_hw <= 0.0:
            return 0.0, 2.0 * math.pi
        a = math.asin(min(slot_hw / r, 1.0))
        return math.pi + a, 3.0 * math.pi - a

    closed = slot_hw <= 0.0
    cols = segs if closed else segs + 1
    rows = []
    for r in (r_in, r_out):
        t0, t1 = angle_range(r)
        row = []
        for i in range(cols):
            s = i / segs
            t = t0 + (t1 - t0) * s
            row.append((r * math.sin(t), r * math.cos(t), r, s))
        rows.append(row)
    verts, uvs = [], []
    for d in (d0, d1):
        for row in rows:
            for x, y, r, s in row:
                verts.append(gp(x, y, d))
                uvs.append(uv_fn(x, y, r, s) if (uv_fn is not None and d == d0) else None)
    fi, fo, bi, bo = 0, cols, 2 * cols, 3 * cols
    faces = []
    for i in range(segs):
        j = (i + 1) % cols
        faces.append((fi + i, fo + i, fo + j, fi + j))
        faces.append((bi + i, bi + j, bo + j, bo + i))
        faces.append((fi + i, fi + j, bi + j, bi + i))
        faces.append((fo + i, bo + i, bo + j, fo + j))
    if not closed:
        for k in (0, cols - 1):
            faces.append((fi + k, bi + k, bo + k, fo + k))
    mb.add_solid(verts, faces, mat, vert_uvs=uvs)


def planar_uv(radius):
    return lambda x, y, r, s: (0.5 + x / (2.0 * radius), 0.5 + y / (2.0 * radius))


def build_gate(mats):
    mb = MeshBuilder(xform=gate_xform(GATE_CENTER))
    hw = GATE_SLOT_HW
    # 太い金の枠と、文字を刻んだ帯（Rune）・縁の盛り上がり
    slotted_annulus(mb, GATE_R_FRAME, GATE_R_OUT, -0.6, 0.6, hw, MAT_GOLD, 160)
    slotted_annulus(mb, GATE_R_FRAME + 0.2, GATE_R_OUT - 0.2, -0.66, -0.58, hw, MAT_RUNE, 160,
                    uv_fn=lambda x, y, r, s: (s * 7.0, (r - GATE_R_FRAME - 0.2) / (GATE_R_OUT - GATE_R_FRAME - 0.4)))
    slotted_annulus(mb, GATE_R_OUT - 0.18, GATE_R_OUT + 0.12, -0.8, 0.5, hw, MAT_GOLD, 160)
    slotted_annulus(mb, GATE_R_FRAME - 0.12, GATE_R_FRAME + 0.18, -0.8, 0.5, hw, MAT_GOLD, 160)
    # 文字盤（ローマ数字は ClockFace テクスチャ）
    slotted_annulus(mb, GATE_R_HOLE, GATE_R_FRAME - 0.05, -0.25, -0.12, hw, MAT_CLOCK, 160,
                    uv_fn=planar_uv(GATE_R_FRAME))
    slotted_annulus(mb, GATE_R_HOLE - 0.15, GATE_R_HOLE + 0.08, -0.4, 0.2, hw, MAT_GOLD, 96)
    # 通り道の両側の金の縁
    y_cut = -math.sqrt(GATE_R_HOLE ** 2 - hw ** 2)
    for sx in (-1.0, 1.0):
        x = sx * (hw + 0.1)
        pts = [gp(x + dx, y, d) for y in (-GATE_R_OUT - 0.1, y_cut) for d, dx in ((-0.75, -0.12), (-0.75, 0.12),
                                                                                 (0.4, 0.12), (0.4, -0.12))]
        # gp は (x, 奥行き, 上) の並びなので、下 4 → 上 4 の順は y でそろう
        mb.hexa([pts[0], pts[1], pts[2], pts[3], pts[4], pts[5], pts[6], pts[7]], MAT_GOLD)
    # 小さな時計（金の胴・縁・文字盤）
    for cx, cy in SMALL_CLOCKS:
        r = SMALL_CLOCK_R
        sub = MeshBuilder(xform=gate_xform((GATE_CENTER[0] + cx, GATE_CENTER[1] + cy, GATE_CENTER[2])))
        slotted_annulus(sub, 0.0001, r - 0.12, -1.02, -0.6, 0.0, MAT_CLOCK, 64, uv_fn=planar_uv(r))
        slotted_annulus(sub, r - 0.15, r + 0.12, -1.12, -0.5, 0.0, MAT_GOLD, 64)
        slotted_annulus(sub, r + 0.05, r + 0.22, -0.95, -0.7, 0.0, MAT_GOLD, 64)
        mb.verts.extend(sub.verts)
        base_face = len(mb.faces)
        offset = len(mb.verts) - len(sub.verts)
        for idx, f in enumerate(sub.faces):
            mb.faces.append([offset + i for i in f])
            mb.mats.append(sub.mats[idx])
            if idx in sub.uvs:
                mb.uvs[base_face + idx] = {offset + k: v for k, v in sub.uvs[idx].items()}
    # 針の軸の飾り
    mb.sphere(gp(0.0, 0.0, -1.35), 0.4, MAT_GOLD, 16, 10)
    # 奥の光（歯車の向こう）
    disc = [gp(4.6 * math.sin(2.0 * math.pi * k / 48), 4.6 * math.cos(2.0 * math.pi * k / 48), 1.0) for k in range(48)]
    mb.poly_facing(disc, None, MAT_LIGHT, (0.0, -1.0, 0.0))
    slot = [gp(-hw, -GATE_R_OUT - 0.2, 1.0), gp(hw, -GATE_R_OUT - 0.2, 1.0), gp(hw, 0.0, 1.0), gp(-hw, 0.0, 1.0)]
    mb.poly_facing(slot, None, MAT_LIGHT, (0.0, -1.0, 0.0))
    return mb.build("HubGate", mats)


def hand_shape(length, width, tail, ring_y, ring_r):
    """針（上向き。原点＝軸）：尖った菱形＋途中の輪。門の面の座標で返す（奥行き 0 の前後に厚み）"""
    poly = [(0.0, -tail), (width * 0.5, 0.0), (width * 0.32, length * 0.75), (0.0, length), (-width * 0.32, length * 0.75),
            (-width * 0.5, 0.0)]
    return poly, ring_y, ring_r


def build_hand(name, length, width, tail, ring_y, ring_r, mats):
    mb = MeshBuilder(xform=gate_xform((0.0, 0.0, 0.0)))
    poly, ry, rr = hand_shape(length, width, tail, ring_y, ring_r)
    n = len(poly)
    verts = [gp(x, y, -0.05) for x, y in poly] + [gp(x, y, 0.05) for x, y in poly]
    faces = [list(range(n)), list(range(n, 2 * n))[::-1]]
    for i in range(n):
        j = (i + 1) % n
        faces.append((i, j, n + j, n + i))
    mb.add_solid(verts, faces, MAT_GOLD)
    pts = [gp(rr * math.cos(2.0 * math.pi * k / 24), ry + rr * math.sin(2.0 * math.pi * k / 24), 0.0) for k in range(25)]
    mb.tube(pts, 0.06 * (length / 6.0) + 0.02, MAT_GOLD)
    return mb.build(name, mats)


def build_gear(mats):
    """半径 1 の歯車（金）。リム・歯 16・ハブ・スポーク 6。エンジンで (r, r, 1) に拡大して使う"""
    mb = MeshBuilder(xform=gate_xform((0.0, 0.0, 0.0)))
    # gate_xform の入力は (x, 奥行き, 上) なので、revolve（Y 軸まわり）＝面の中の円
    mb.ring_rect(0.74, 0.9, -0.12, 0.12, 64, MAT_GOLD)
    for k in range(16):
        t = 2.0 * math.pi * k / 16
        mb.radial_box(t, 0.88, 1.04, -0.1, 0.1, 0.07, MAT_GOLD)
    mb.ring_rect(0.0001, 0.2, -0.16, 0.16, 24, MAT_GOLD)
    for k in range(6):
        t = 2.0 * math.pi * k / 6
        mb.radial_box(t, 0.15, 0.78, -0.06, 0.06, 0.05, MAT_GOLD)
    return mb.build("HubGear", mats)


def place_gate_mover(obj, x, y, depth, scale, angle):
    rot = P_E2B @ engine_rotation(0.0, 0.0, angle) @ np.diag([scale, scale, 1.0]) @ P_E2B.T
    m = Matrix(rot.tolist()).to_4x4()
    m.translation = Vector(e2b((GATE_CENTER[0] + x, GATE_CENTER[1] + y, GATE_CENTER[2] + depth)))
    obj.matrix_world = m


# ============================================================
# 天球儀の輪（ローカル：輪の面の法線が X。原点＝輪の中心）
# ============================================================
def to_normal_x(p):
    """Y を法線にして組んだ輪を、X が法線になるように倒す"""
    return (p[1], -p[0], p[2])


def build_ring(name, radius, half_w, half_t, planets, mats):
    mb = MeshBuilder(xform=to_normal_x)
    mb.ring_rect(radius - half_t, radius + half_t, -half_w, half_w, 160, MAT_GOLD)
    for k in range(72):
        t = math.radians(5.0 * k)
        long_tick = k % 6 == 0
        hy = half_w * (0.95 if long_tick else 0.55)
        mb.radial_box(t, radius - half_t - 0.05, radius - half_t + 0.01, -hy, hy, 0.05 if long_tick else 0.025, MAT_DARK)
    for k in range(12):
        t = math.radians(30.0 * k + 15.0)
        x, _, z = polar(radius, t, 0.0)
        mb.cylinder(x, z, half_t * 1.6, half_w - 0.01, half_w + 0.05, 16, MAT_MARBLE)
        mb.cylinder(x, z, half_t * 1.6, -half_w - 0.05, -half_w + 0.01, 16, MAT_MARBLE)
    for k in range(planets):
        t = 2.0 * math.pi * k / planets + 0.4
        mb.sphere(polar(radius + half_t + 0.35, t, 0.0), 0.5, MAT_MARBLE if k % 2 == 0 else MAT_DARK, 16, 10)
    return mb.build(name, mats)


def place_ring_preview(obj, center, tilt_y, tilt_z, spin):
    rot = P_E2B @ engine_rotation(spin, tilt_y, tilt_z) @ P_E2B.T
    m = Matrix(rot.tolist()).to_4x4()
    m.translation = Vector(e2b(center))
    obj.matrix_world = m


def check_rings():
    """輪が機体・足場・視点の通り道・床に近すぎないか数値で確かめる（近すぎたら止める）"""
    mech = load_mech_points()
    for name, radius, center, ty, tz, _spin, half_w, half_t, planets in RINGS:
        t = np.linspace(0.0, 2.0 * math.pi, 720, endpoint=False)
        local = np.stack([np.zeros_like(t), radius * np.cos(t), radius * np.sin(t)], 1)
        pts = local @ engine_rotation(0.0, ty, tz).T + np.asarray(center)
        margin = half_w + half_t + (0.85 if planets else 0.0)
        lo, hi = np.asarray(CORRIDOR_MIN), np.asarray(CORRIDOR_MAX)
        corridor = np.linalg.norm(np.maximum(np.maximum(lo - pts, pts - hi), 0.0), axis=1).min() - margin
        floor = pts[:, 1].min() - margin
        mech_d = float("inf")
        if mech is not None:
            for p in pts[::4]:
                mech_d = min(mech_d, np.sqrt(((mech - p) ** 2).sum(1)).min())
            mech_d -= margin
        print(f"{LOG} {name}: corridor {corridor:.2f} m, mech {mech_d:.2f} m, floor {floor:.2f} m")
        if corridor < RING_CLEARANCE or mech_d < RING_CLEARANCE or floor < 0.2:
            raise SystemExit(f"{LOG} {name} が近すぎる（RINGS を直すこと）")


def load_mech_points():
    if not MECH_GLTF.exists():
        print(f"{LOG} {MECH_GLTF} が無いので機体との距離は確かめない")
        return None
    g = json.loads(MECH_GLTF.read_text(encoding="utf-8"))
    data = (MECH_GLTF.parent / g["buffers"][0]["uri"]).read_bytes()
    pts = []
    for mesh in g["meshes"]:
        for prim in mesh["primitives"]:
            acc = g["accessors"][prim["attributes"]["POSITION"]]
            view = g["bufferViews"][acc["bufferView"]]
            off = view.get("byteOffset", 0) + acc.get("byteOffset", 0)
            stride = view.get("byteStride", 12)
            raw = np.frombuffer(data, np.uint8, acc["count"] * stride, off).reshape(-1, stride)[:, :12]
            pts.append(raw.copy().view(np.float32).reshape(-1, 3))
    p = np.concatenate(pts)[::9].astype(np.float64)
    p[:, 0] *= -1.0   # cook と同じく X を反転＝エンジン
    return p


# ============================================================
# テクスチャ（numpy。配列は行 0 = 画像の上）
# ============================================================
def tileable_noise(rng, n, beta):
    white = rng.standard_normal((n, n))
    fx = np.fft.fftfreq(n)[None, :]
    fy = np.fft.fftfreq(n)[:, None]
    r = np.sqrt(fx * fx + fy * fy)
    r[0, 0] = 1.0
    spec = np.fft.fft2(white) / (r ** beta)
    spec[0, 0] = 0.0
    out = np.real(np.fft.ifft2(spec))
    return (out - out.min()) / (out.max() - out.min())


def marble_fields(n, seed, base_srgb, vein_srgb, strength, sharpness, haze_srgb=None):
    """gen_title_door.py と同じ作り方の大理石（色違いを作れるよう引数にした）"""
    rng = np.random.default_rng(seed)
    warp = tileable_noise(rng, n, 2.2)
    warp2 = tileable_noise(rng, n, 2.0)
    cloud = tileable_noise(rng, n, 1.7)
    y, x = np.mgrid[0:n, 0:n] / n

    def veins(kx, ky, w, field, sharp):
        phase = 2.0 * np.pi * (kx * x + ky * y) + w * 2.0 * np.pi * field
        return (1.0 - np.abs(np.sin(phase))) ** sharp

    main = veins(1, 2, 1.6, warp, sharpness)
    sub = veins(3, -1, 1.1, warp2, sharpness * 1.8) * 0.35
    v = np.clip(main + sub, 0.0, 1.0)
    haze = np.clip((cloud - 0.45) * 1.6, 0.0, 1.0) * 0.25
    base = np.array(base_srgb)[None, None, :] + (cloud[..., None] - 0.5) * 0.04
    if haze_srgb is not None:
        # 地のむらに淡い色（虹色寄りの大理石）
        base = base * (1.0 - haze[..., None] * 1.6) + np.array(haze_srgb)[None, None, :] * haze[..., None] * 1.6
    mix = np.clip(v * strength + (haze if haze_srgb is None else 0.0), 0.0, 1.0)[..., None]
    color = base * (1.0 - mix) + np.array(vein_srgb)[None, None, :] * mix
    height = -v * 0.3 + (cloud - 0.5) * 0.1
    return np.clip(color, 0.0, 1.0), height


def height_to_normal(height, strength):
    dx = (np.roll(height, -1, axis=1) - np.roll(height, 1, axis=1)) * 0.5
    dy = (np.roll(height, -1, axis=0) - np.roll(height, 1, axis=0)) * 0.5
    n = height.shape[0]
    nx = -dx * strength * n / 64.0
    ny = -dy * strength * n / 64.0
    nz = np.ones_like(height)
    length = np.sqrt(nx * nx + ny * ny + nz * nz)
    return np.stack([nx / length, ny / length, nz / length], axis=-1) * 0.5 + 0.5


def smooth_step(e0, e1, x):
    t = np.clip((x - e0) / (e1 - e0), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def draw_segments(mask, segments, width):
    """mask（h, w）に太さ width [px] の線分を描く（最大値で重ねる）。座標は px（x 右・y 下）"""
    h, w = mask.shape
    for (x0, y0), (x1, y1) in segments:
        xa, xb = int(max(min(x0, x1) - width - 2, 0)), int(min(max(x0, x1) + width + 2, w - 1))
        ya, yb = int(max(min(y0, y1) - width - 2, 0)), int(min(max(y0, y1) + width + 2, h - 1))
        if xa > xb or ya > yb:
            continue
        yy, xx = np.mgrid[ya:yb + 1, xa:xb + 1].astype(np.float64)
        dx, dy = x1 - x0, y1 - y0
        L2 = max(dx * dx + dy * dy, 1e-9)
        t = np.clip(((xx - x0) * dx + (yy - y0) * dy) / L2, 0.0, 1.0)
        d = np.hypot(xx - (x0 + t * dx), yy - (y0 + t * dy))
        v = 1.0 - smooth_step(width * 0.5 - 0.7, width * 0.5 + 0.7, d)
        mask[ya:yb + 1, xa:xb + 1] = np.maximum(mask[ya:yb + 1, xa:xb + 1], v)


ROMAN_STROKES = {
    "I": [((0.0, 0.0), (0.0, 1.0))],
    "V": [((-0.3, 0.0), (0.0, 1.0)), ((0.3, 0.0), (0.0, 1.0))],
    "X": [((-0.3, 0.0), (0.3, 1.0)), ((0.3, 0.0), (-0.3, 1.0))],
}
ROMAN = ["XII", "I", "II", "III", "IV", "V", "VI", "VII", "VIII", "IX", "X", "XI"]


def clock_face(n):
    """文字盤（平面の UV で円に貼る）。ローマ数字は外向きに並べる（参考画像と同じ）。行 0 = 上"""
    c = (np.arange(n) + 0.5) / n * 2.0 - 1.0
    X, Y = c[None, :], -c[:, None]
    r = np.hypot(X, Y)
    ivory = np.array([0.95, 0.93, 0.88])
    color = np.tile(ivory, (n, n, 1)) * (1.0 - 0.08 * r[..., None] ** 2)
    ink = np.zeros((n, n))
    px = 2.0 / n
    for rr, wd in ((0.985, 0.012), (0.955, 0.004), (0.70, 0.006), (0.66, 0.003)):
        ink = np.maximum(ink, 1.0 - smooth_step(wd * 0.5 - px, wd * 0.5 + px, np.abs(r - rr)))
    segs, thick = [], []
    for k in range(60):
        a = 2.0 * math.pi * k / 60
        long_tick = k % 5 == 0
        r0 = 0.90 if long_tick else 0.925
        segs.append(((r0 * math.sin(a), r0 * math.cos(a)), (0.95 * math.sin(a), 0.95 * math.cos(a))))
        thick.append(0.012 if long_tick else 0.006)
    for (p0, p1), wd in zip(segs, thick):
        draw_segments(ink, [(((p0[0] + 1) * n / 2, (1 - p0[1]) * n / 2), ((p1[0] + 1) * n / 2, (1 - p1[1]) * n / 2))],
                      wd * n / 2)
    # ローマ数字（縦 0.11、外向き）
    for k, text in enumerate(ROMAN):
        a = 2.0 * math.pi * k / 12
        out = np.array([math.sin(a), math.cos(a)])
        side = np.array([math.cos(a), -math.sin(a)])
        base = out * 0.745
        h = 0.11
        gap = 0.045
        width = sum(0.6 if ch != "I" else 0.0 for ch in text) * h + gap * (len(text) - 1)
        x = -width * 0.5
        lines = []
        for ch in text:
            cw = 0.0 if ch == "I" else 0.6 * h
            cx = x + cw * 0.5
            for (sx0, sy0), (sx1, sy1) in ROMAN_STROKES[ch]:
                q0 = base + side * (cx + sx0 * h) + out * (sy0 * h)
                q1 = base + side * (cx + sx1 * h) + out * (sy1 * h)
                lines.append(((q0[0] + 1) * n / 2, (1 - q0[1]) * n / 2, (q1[0] + 1) * n / 2, (1 - q1[1]) * n / 2))
            # 上下のセリフ
            for sy in (0.0, 1.0):
                q0 = base + side * (cx - 0.22 * h - cw * 0.5) + out * (sy * h)
                q1 = base + side * (cx + 0.22 * h + cw * 0.5) + out * (sy * h)
                lines.append(((q0[0] + 1) * n / 2, (1 - q0[1]) * n / 2, (q1[0] + 1) * n / 2, (1 - q1[1]) * n / 2))
            x += cw + gap
        draw_segments(ink, [((l[0], l[1]), (l[2], l[3])) for l in lines], 0.011 * n / 2)
    gold = np.array([0.55, 0.38, 0.12])
    color = color * (1.0 - ink[..., None]) + gold * ink[..., None]
    return np.clip(color, 0.0, 1.0)


def rune_band(w, h, seed):
    """門の枠の文字の帯：赤銅色の地に金の文字（ルーン風。線を 2〜4 本組み合わせる）。横＝輪に沿う"""
    rng = np.random.default_rng(seed)
    base = np.array([0.42, 0.14, 0.09])
    noise = rng.random((h, w)) * 0.05
    color = np.tile(base, (h, w, 1)) * (0.9 + noise[..., None])
    ink = np.zeros((h, w))
    draw_segments(ink, [((0, h * 0.08), (w, h * 0.08)), ((0, h * 0.92), (w, h * 0.92))], h * 0.05)
    cell = h * 0.42
    grid = [(gx, gy) for gx in (0.0, 0.5, 1.0) for gy in (0.0, 0.33, 0.66, 1.0)]
    x = cell * 0.3
    while x < w - cell:
        segs = []
        for _ in range(int(rng.integers(2, 5))):
            a, b = rng.choice(len(grid), 2, replace=False)
            (ax, ay), (bx, by) = grid[a], grid[b]
            segs.append(((x + ax * cell * 0.6, h * 0.2 + ay * h * 0.6), (x + bx * cell * 0.6, h * 0.2 + by * h * 0.6)))
        draw_segments(ink, segs, h * 0.045)
        x += cell
    gold = np.array([0.95, 0.72, 0.30])
    return np.clip(color * (1.0 - ink[..., None]) + gold * ink[..., None], 0.0, 1.0)


def pastel(h, s=0.32, v=0.97):
    return np.array(colorsys.hsv_to_rgb(h % 1.0, s, v))


def glass_fields(w, h):
    """とがったアーチの窓のガラス：淡い虹色＋ひし形の鉛の線。u＝横、v（下から）＝高さ"""
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float64)
    u, v = xx / w, 1.0 - yy / h
    hue = (0.52 + 0.55 * v + 0.12 * u) % 1.0
    s = 0.30 + 0.08 * np.sin(u * 9.0 + v * 5.0)
    rgb = np.stack(np.vectorize(colorsys.hsv_to_rgb)(hue, s, np.full_like(hue, 0.96)), axis=-1)
    cell = w / 4.0
    a = ((xx + yy * 0.5) % cell) / cell
    b = ((xx - yy * 0.5) % cell) / cell
    lead = np.maximum(1.0 - smooth_step(0.0, 0.03, np.minimum(a, 1 - a)), 1.0 - smooth_step(0.0, 0.03, np.minimum(b, 1 - b)))
    lead_color = np.array([0.55, 0.45, 0.25])
    return np.clip(rgb * (1.0 - lead[..., None]) + lead_color * lead[..., None], 0.0, 1.0)


def cloth_fields(w, h):
    """垂れ幕：上の薄紫から下のピンク（ひだは形で出す）。v（下から）"""
    v = np.linspace(1.0, 0.0, h)[:, None, None]
    top = np.array([0.72, 0.62, 0.92])
    bottom = np.array([0.97, 0.72, 0.86])
    col = top * v + bottom * (1.0 - v)
    stripe = 0.94 + 0.06 * np.cos(np.linspace(0.0, 2.0 * np.pi * 14, w))[None, :, None]
    return np.clip(np.tile(col, (1, w, 1)) * stripe, 0.0, 1.0)


def step_fields(w):
    """大階段の段：段ごとに u で色を拾う淡い虹色の帯"""
    row = np.stack([pastel(0.45 + 0.9 * i / w, 0.35, 0.98) for i in range(w)])
    return np.tile(row[None, :, :], (8, 1, 1))


def flower_fields():
    colors = [(0.25, 0.42, 0.92), (0.45, 0.65, 0.98), (0.97, 0.97, 1.0), (1.0, 0.86, 0.35),
              (0.80, 0.62, 0.95), (0.95, 0.70, 0.85), (0.30, 0.55, 0.30), (0.2, 0.4, 0.22)]
    return np.repeat(np.array(colors)[None, :, :], 8, axis=0).repeat(16, axis=1)


def floor_fields(n):
    """鏡の床の模様（虹色の黄道の帯つき）。行＝エンジンの z、列＝x。原点がテクスチャの角"""
    px = FLOOR_TILE_M / n
    idx = (np.arange(n) + 0.5) / n
    w = (idx + 0.5) % 1.0 - 0.5
    X = w[None, :] * FLOOR_TILE_M
    Z = w[:, None] * FLOOR_TILE_M
    r = np.hypot(X, Z)
    th = np.arctan2(X, Z)

    white, _ = marble_fields(512, 31, (0.95, 0.94, 0.97), (0.74, 0.72, 0.88), 0.32, 4.0, (0.92, 0.86, 0.95))
    lilac, _ = marble_fields(512, 33, (0.80, 0.74, 0.92), (0.62, 0.58, 0.82), 0.4, 4.0)
    deep, _ = marble_fields(512, 32, (0.20, 0.17, 0.38), (0.52, 0.50, 0.75), 0.5, 5.0)
    reps = n // 512
    white = np.tile(white, (reps, reps, 1))
    lilac = np.tile(lilac, (reps, reps, 1))
    deep = np.tile(deep, (reps, reps, 1))
    gold = np.array([0.85, 0.63, 0.28])

    def line(dist, width):
        return 1.0 - smooth_step(width * 0.5 - px, width * 0.5 + px, np.abs(dist))

    d = 1.0 - smooth_step(4.0 - px, 4.0 + px, r)
    color = white * (1.0 - d[..., None]) + deep * d[..., None]
    seg = np.floor((th + np.pi) / (2.0 * np.pi / 24.0)).astype(int) % 2
    band = smooth_step(12.0 - px, 12.0 + px, r) * (1.0 - smooth_step(16.0 - px, 16.0 + px, r)) * seg
    color = color * (1.0 - band[..., None]) + lilac * band[..., None]
    # 黄道の帯：12 区画を淡い虹色に
    sector = 2.0 * np.pi / 12.0
    zod = smooth_step(7.0 - px, 7.0 + px, r) * (1.0 - smooth_step(11.5 - px, 11.5 + px, r))
    k = (np.floor((th + np.pi) / sector).astype(int)) % 12
    tint = np.stack([pastel(i / 12.0, 0.22, 1.0) for i in range(12)])[k]
    color = color * (1.0 - zod[..., None] * 0.6) + color * tint * zod[..., None] * 0.6

    g = np.zeros_like(r)
    for rr, wd in ((4.0, 0.1), (4.3, 0.05), (7.0, 0.08), (11.5, 0.06), (12.0, 0.1), (16.0, 0.1),
                   (16.3, 0.05), (19.0, 0.06), (21.6, 0.12)):
        g = np.maximum(g, line(r - rr, wd))
    in_zodiac = (r > 7.0) & (r < 11.5)
    rel = (th + sector * 0.5) % sector - sector * 0.5
    g = np.maximum(g, line(rel * r, 0.06) * in_zodiac)
    for kk in range(12):
        a = sector * kk + sector * 0.5
        cx, cz = 9.25 * math.sin(a), 9.25 * math.cos(a)
        rs = np.hypot(X - cx, Z - cz)
        phi = np.arctan2(X - cx, Z - cz) - a
        star = 0.28 + 0.62 * np.abs(np.cos(phi * 4.0)) ** 6
        g = np.maximum(g, 1.0 - smooth_step(star - px, star + px, rs))
    rose = 0.4 + 3.2 * np.abs(np.cos(th * 2.0)) ** 14 + 1.6 * np.abs(np.sin(th * 2.0)) ** 14
    g = np.maximum(g, (1.0 - smooth_step(rose - px, rose + px, r)) * 0.9)
    rng = np.random.default_rng(5)
    for _ in range(140):
        rr = rng.uniform(16.6, 18.7)
        a = rng.uniform(0.0, 2.0 * np.pi)
        size = rng.uniform(0.03, 0.09)
        rs = np.hypot(X - rr * math.sin(a), Z - rr * math.cos(a))
        g = np.maximum(g, 1.0 - smooth_step(size - px, size + px, rs))
    color = color * (1.0 - g[..., None]) + gold * g[..., None]
    return np.clip(color, 0.0, 1.0)


def save_png(name, rgb, out_dir, non_color):
    """rgb: (h, w, 3) 0..1、行 0 = 上。Blender の pixels は下の行から"""
    h, w, _ = rgb.shape
    img = bpy.data.images.new(name, width=w, height=h, alpha=False)
    if non_color:
        img.colorspace_settings.name = "Non-Color"
    rgba = np.concatenate([np.flipud(rgb), np.ones((h, w, 1))], axis=-1).astype(np.float32)
    img.pixels.foreach_set(rgba.ravel())
    path = out_dir / f"{name}.png"
    img.filepath_raw = str(path.resolve())
    img.file_format = "PNG"
    img.save()
    print(f"{LOG} texture {path}")
    return img


def make_textures(out_dir, with_floor):
    out_dir.mkdir(parents=True, exist_ok=True)
    white, wh = marble_fields(TEX_SIZE, 7, (0.95, 0.94, 0.97), (0.72, 0.70, 0.88), 0.4, 4.0, (0.92, 0.85, 0.96))
    dark, dh = marble_fields(TEX_SIZE, 9, (0.20, 0.17, 0.36), (0.55, 0.52, 0.76), 0.5, 5.0)
    flat = np.tile(np.array([0.5, 0.5, 1.0]), (4, 4, 1))
    tex = {
        "Marble": save_png("Marble_BaseColor", white, out_dir, False),
        "Marble_n": save_png("Marble_NormalMap", height_to_normal(wh, 0.25), out_dir, True),
        "MarbleDark": save_png("MarbleDark_BaseColor", dark, out_dir, False),
        "MarbleDark_n": save_png("MarbleDark_NormalMap", height_to_normal(dh, 0.25), out_dir, True),
        "flat_n": save_png("Gold_NormalMap", flat, out_dir, True),
        "Glass": save_png("Glass_BaseColor", glass_fields(512, 1024), out_dir, False),
        "Cloth": save_png("Cloth_BaseColor", cloth_fields(256, 512), out_dir, False),
        "Rune": save_png("Rune_BaseColor", rune_band(2048, 128, 3), out_dir, False),
        "ClockFace": save_png("ClockFace_BaseColor", clock_face(2048), out_dir, False),
        "Step": save_png("Step_BaseColor", step_fields(256), out_dir, False),
        "Flowers": save_png("Flowers_BaseColor", flower_fields(), out_dir, False),
        "Light": save_png("Light_BaseColor", np.tile(np.array([1.0, 0.95, 0.85]), (4, 4, 1)), out_dir, False),
    }
    if with_floor:
        tex["floor"] = save_png("HubFloor_BaseColor", floor_fields(FLOOR_TEX), out_dir, False)
    return tex


# ============================================================
# マテリアル（glTF にそのまま出る構成：画像 → Base Color / Normal Map）
# ============================================================
def attach_normal_map(nt, bsdf, image):
    tex = nt.nodes.new("ShaderNodeTexImage")
    tex.image = image
    nmap = nt.nodes.new("ShaderNodeNormalMap")
    nt.links.new(tex.outputs["Color"], nmap.inputs["Color"])
    nt.links.new(nmap.outputs["Normal"], bsdf.inputs["Normal"])


def textured_material(name, base_img, normal_img, roughness, emission=0.0):
    """emission はプレビューと IBL の撮影の中だけ光らせる（cook は発光を読まない）"""
    mat = bpy.data.materials.new(name)
    mat.use_nodes = True
    nt = mat.node_tree
    bsdf = nt.nodes["Principled BSDF"]
    bsdf.inputs["Roughness"].default_value = roughness
    bsdf.inputs["Metallic"].default_value = 0.0
    base = nt.nodes.new("ShaderNodeTexImage")
    base.image = base_img
    nt.links.new(base.outputs["Color"], bsdf.inputs["Base Color"])
    if emission > 0.0:
        nt.links.new(base.outputs["Color"], bsdf.inputs["Emission Color"])
        bsdf.inputs["Emission Strength"].default_value = emission
    attach_normal_map(nt, bsdf, normal_img)
    return mat


def make_materials(tex):
    flat = tex["flat_n"]
    gold = bpy.data.materials.new("Gold")
    gold.use_nodes = True
    g = gold.node_tree.nodes["Principled BSDF"]
    g.inputs["Base Color"].default_value = (*GOLD_SRGB, 1.0)
    g.inputs["Metallic"].default_value = 1.0
    g.inputs["Roughness"].default_value = 0.28
    attach_normal_map(gold.node_tree, g, flat)
    mats = {
        "Marble": textured_material("Marble", tex["Marble"], tex["Marble_n"], 0.3),
        "MarbleDark": textured_material("MarbleDark", tex["MarbleDark"], tex["MarbleDark_n"], 0.25),
        "Gold": gold,
        "Glass": textured_material("Glass", tex["Glass"], flat, 0.15, emission=2.5),
        "Cloth": textured_material("Cloth", tex["Cloth"], flat, 0.8),
        "Rune": textured_material("Rune", tex["Rune"], flat, 0.4),
        "ClockFace": textured_material("ClockFace", tex["ClockFace"], flat, 0.35),
        "Step": textured_material("Step", tex["Step"], flat, 0.2, emission=0.6),
        "Flowers": textured_material("Flowers", tex["Flowers"], flat, 0.6),
        "Light": textured_material("Light", tex["Light"], flat, 0.5, emission=3.0),
    }
    return [mats[n] for n in MAT_NAMES]


# ============================================================
# 書き出し
# ============================================================
def flip_texcoord_v(gltf_path):
    """cook は glTF の V を必ず反転するので、その逆を書いておく（rig_hub_mech.py と同じ。17 番 §5.1 の罠）"""
    gltf = json.loads(gltf_path.read_text(encoding="utf-8"))
    ids = {p["attributes"]["TEXCOORD_0"] for m in gltf.get("meshes", []) for p in m["primitives"]
           if "TEXCOORD_0" in p.get("attributes", {})}
    if not ids:
        return
    bin_path = gltf_path.parent / gltf["buffers"][0]["uri"]
    data = bytearray(bin_path.read_bytes())
    for ai in ids:
        acc = gltf["accessors"][ai]
        view = gltf["bufferViews"][acc["bufferView"]]
        base = view.get("byteOffset", 0) + acc.get("byteOffset", 0)
        stride = view.get("byteStride", 8)
        for i in range(acc["count"]):
            o = base + i * stride + 4
            (v,) = struct.unpack_from("<f", data, o)
            struct.pack_into("<f", data, o, 1.0 - v)
        if "min" in acc and "max" in acc:
            acc["min"][1], acc["max"][1] = 1.0 - acc["max"][1], 1.0 - acc["min"][1]
    bin_path.write_bytes(bytes(data))
    gltf_path.write_text(json.dumps(gltf, indent=2), encoding="utf-8")


def export_parts(out_dir, objs):
    out_dir.mkdir(parents=True, exist_ok=True)
    for obj in objs:
        saved = obj.matrix_world.copy()
        obj.matrix_world = Matrix.Identity(4)
        bpy.ops.object.select_all(action="DESELECT")
        obj.select_set(True)
        bpy.context.view_layer.objects.active = obj
        path = out_dir / f"{obj.name}.gltf"
        bpy.ops.export_scene.gltf(filepath=str(path.resolve()), export_format="GLTF_SEPARATE",
                                  use_selection=True, export_yup=True, export_apply=True,
                                  export_materials="EXPORT", export_animations=False)
        obj.matrix_world = saved
        flip_texcoord_v(path)
        print(f"{LOG} exported {path}")


def report_stats(objs):
    total = 0
    for obj in objs:
        tris = sum(len(p.vertices) - 2 for p in obj.data.polygons)
        total += tris
        print(f"{LOG} {obj.name}: {tris} tris")
    print(f"{LOG} total {total} tris")


# ============================================================
# 光（プレビューと IBL の撮影）
# ============================================================
def setup_lighting(floor_img):
    scene = bpy.context.scene
    world = bpy.data.worlds.new("Sky")
    scene.world = world
    world.use_nodes = True
    nt = world.node_tree
    sky = nt.nodes.new("ShaderNodeTexSky")
    sky.sky_type = "NISHITA"
    sky.sun_elevation = math.radians(55.0)
    sky.sun_disc = False
    nt.links.new(sky.outputs["Color"], nt.nodes["Background"].inputs["Color"])
    nt.nodes["Background"].inputs["Strength"].default_value = 0.6

    sun = bpy.data.objects.new("Sun", bpy.data.lights.new("Sun", "SUN"))
    sun.data.energy = 6.0
    sun.data.color = (1.0, 0.95, 0.85)
    travel = Vector(e2b((-0.191, -0.815, -0.547)))
    sun.rotation_euler = travel.to_track_quat("-Z", "Y").to_euler()
    scene.collection.objects.link(sun)

    top = bpy.data.objects.new("Oculus", bpy.data.lights.new("Oculus", "AREA"))
    top.data.shape = "DISK"
    top.data.size = OCULUS_R * 2.0
    top.data.energy = 60000.0
    top.data.color = (1.0, 0.98, 0.96)
    top.location = e2b((0.0, 43.0, 0.0))
    scene.collection.objects.link(top)
    # 窓の側から淡い水色とピンク（パルテナの虹色の光）
    for k in range(8):
        a = 2.0 * math.pi * k / 8.0 + 0.2
        lamp = bpy.data.objects.new(f"Tint{k}", bpy.data.lights.new(f"Tint{k}", "POINT"))
        lamp.data.energy = 2500.0
        lamp.data.color = (0.65, 0.85, 1.0) if k % 2 == 0 else (1.0, 0.72, 0.88)
        lamp.data.shadow_soft_size = 2.0
        lamp.location = e2b(polar(15.0, a, 15.0))
        scene.collection.objects.link(lamp)
    gate = bpy.data.objects.new("GateGlow", bpy.data.lights.new("GateGlow", "POINT"))
    gate.data.energy = 1500.0
    gate.data.color = (1.0, 0.9, 0.7)
    gate.data.shadow_soft_size = 3.0
    gate.location = e2b((0.0, 19.0, 17.0))
    scene.collection.objects.link(gate)

    bpy.ops.mesh.primitive_circle_add(vertices=128, radius=TEMPLE_R + 0.1, fill_type="NGON", location=(0, 0, 0))
    floor = bpy.context.active_object
    floor.name = "FloorPreview"
    mat = bpy.data.materials.new("FloorPreview")
    mat.use_nodes = True
    nt = mat.node_tree
    b = nt.nodes["Principled BSDF"]
    b.inputs["Roughness"].default_value = 0.04
    b.inputs["Coat Weight"].default_value = 0.8
    if floor_img is not None:
        tex = nt.nodes.new("ShaderNodeTexImage")
        tex.image = floor_img
        coord = nt.nodes.new("ShaderNodeTexCoord")
        mapping = nt.nodes.new("ShaderNodeMapping")
        # Blender (x, y) = エンジン (−x, −z)。テクスチャの u = x / 44、v（上から）= z / 44
        mapping.inputs["Scale"].default_value = (-1.0 / FLOOR_TILE_M, 1.0 / FLOOR_TILE_M, 1.0)
        nt.links.new(coord.outputs["Object"], mapping.inputs["Vector"])
        nt.links.new(mapping.outputs["Vector"], tex.inputs["Vector"])
        nt.links.new(tex.outputs["Color"], b.inputs["Base Color"])
    floor.data.materials.append(mat)
    return floor


def setup_render(width, height, samples):
    scene = bpy.context.scene
    scene.render.engine = "CYCLES"
    scene.cycles.device = "CPU"
    scene.cycles.samples = samples
    scene.cycles.use_denoising = True
    scene.cycles.max_bounces = 6
    scene.render.resolution_x = width
    scene.render.resolution_y = height
    scene.view_settings.view_transform = "AgX"
    scene.view_settings.look = "AgX - Medium High Contrast"
    scene.view_settings.exposure = -1.3


def set_camera(eye, target, fov_y_deg=60.0):
    scene = bpy.context.scene
    cam = scene.camera
    if cam is None:
        cam = bpy.data.objects.new("Camera", bpy.data.cameras.new("Camera"))
        scene.collection.objects.link(cam)
        scene.camera = cam
    cam.data.type = "PERSP"
    cam.data.sensor_fit = "VERTICAL"
    cam.data.angle_y = math.radians(fov_y_deg)
    cam.data.clip_end = 300.0
    cam.location = e2b(eye)
    cam.rotation_euler = (Vector(e2b(target)) - Vector(e2b(eye))).to_track_quat("-Z", "Y").to_euler()


def render(path):
    bpy.context.scene.render.filepath = str(Path(path).resolve())
    bpy.ops.render.render(write_still=True)
    print(f"{LOG} wrote {path}")


def import_mech():
    for name in ("HubMech", "HubMech_Cockpit"):
        bpy.ops.import_scene.gltf(filepath=str((MECH_GLTF.parent / f"{name}.gltf").resolve()))
    print(f"{LOG} imported mech from {MECH_GLTF.parent}")


def import_title_door():
    """プレビューにタイトルの扉（2 倍）を門の通り道に置く。エンジンでは HubTemple が置く"""
    folder = Path("Assets/Models/TitleDoor")
    if not folder.exists():
        return
    for name, offset in (("door_frame", (0.0, 0.0, 0.0)), ("door_leaf_L", (0.9, 0.0, -0.2)), ("door_leaf_R", (-0.9, 0.0, -0.2))):
        before = set(bpy.data.objects)
        bpy.ops.import_scene.gltf(filepath=str((folder / f"{name}.gltf").resolve()))
        for obj in set(bpy.data.objects) - before:
            if obj.parent is None:
                obj.scale = (2.0, 2.0, 2.0)
                obj.location = Vector(e2b((offset[0] * 2.0, LANDING_Y + offset[1], GATE_CENTER[2] - 0.2 + offset[2] * 2.0)))


def render_ibl(path):
    """機体の胸の位置から正距円筒で撮り、平均の明るさを IBL_MEAN にそろえて .hdr で保存する"""
    scene = bpy.context.scene
    setup_render(*IBL_SIZE, 128)
    cam = scene.camera
    if cam is None:
        cam = bpy.data.objects.new("Camera", bpy.data.cameras.new("Camera"))
        scene.collection.objects.link(cam)
        scene.camera = cam
    cam.data.type = "PANO"
    cam.data.panorama_type = "EQUIRECTANGULAR"
    cam.location = e2b(IBL_EYE)
    # gen_title_sky.py（今の IBL・空）と同じ向き。方位がエンジンとそろっているかは実機の映り込みで確かめる
    cam.rotation_euler = (math.radians(90.0), 0.0, 0.0)
    scene.view_settings.view_transform = "Standard"
    scene.view_settings.look = "None"
    scene.view_settings.exposure = 0.0
    tmp = Path(bpy.app.tempdir) / "hub_ibl_raw.exr"
    scene.render.image_settings.file_format = "OPEN_EXR"
    scene.render.image_settings.color_depth = "32"
    scene.render.filepath = str(tmp)
    bpy.ops.render.render(write_still=True)

    img = bpy.data.images.load(str(tmp))
    px = np.empty(img.size[0] * img.size[1] * 4, dtype=np.float32)
    img.pixels.foreach_get(px)
    px = px.reshape(-1, 4)
    lum = px[:, :3] @ np.array([0.2126, 0.7152, 0.0722])
    scale = IBL_MEAN / max(float(lum.mean()), 1e-6)
    px[:, :3] *= scale
    print(f"{LOG} IBL mean {lum.mean():.4f} -> x{scale:.3f}")
    out = bpy.data.images.new("hub_ibl", width=img.size[0], height=img.size[1], alpha=False, float_buffer=True)
    out.pixels.foreach_set(px.ravel())
    out.filepath_raw = str(Path(path).resolve())
    out.file_format = "HDR"
    out.save()
    print(f"{LOG} wrote IBL {path}")


# ============================================================
def arg_path(argv, name):
    return Path(argv[argv.index(name) + 1]) if name in argv else None


def main():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    export_dir = arg_path(argv, "--export")
    ibl_path = arg_path(argv, "--ibl")
    preview_dir = arg_path(argv, "--preview")
    save_path = arg_path(argv, "--save")

    bpy.ops.wm.read_factory_settings(use_empty=True)
    check_rings()

    tex_dir = export_dir if export_dir is not None else Path(bpy.app.tempdir) / "hub_temple_tex"
    want_floor = export_dir is not None or preview_dir is not None or ibl_path is not None
    tex = make_textures(tex_dir, want_floor)
    mats = make_materials(tex)
    rng = np.random.default_rng(17)
    shell = build_shell(mats, rng)
    gate = build_gate(mats)
    movers = {
        "HubGate_MinuteHand": build_hand("HubGate_MinuteHand", 6.4, 0.42, 0.9, 4.3, 0.42, mats),
        "HubGate_HourHand": build_hand("HubGate_HourHand", 4.3, 0.55, 0.7, 2.7, 0.38, mats),
        "HubGate_SmallHand": build_hand("HubGate_SmallHand", 1.45, 0.16, 0.25, 0.95, 0.12, mats),
        "HubGear": build_gear(mats),
    }
    rings = []
    for name, radius, center, ty, tz, spin, half_w, half_t, planets in RINGS:
        rings.append((build_ring(name, radius, half_w, half_t, planets, mats), center, ty, tz, spin))
    parts = [shell, gate] + list(movers.values()) + [r[0] for r in rings]
    report_stats(parts)

    if export_dir is not None:
        export_parts(export_dir, parts)

    # ここから先はプレビュー・IBL 用（輪・門の動く部分を置き、床と光を足す）
    for ring, center, ty, tz, spin in rings:
        place_ring_preview(ring, center, ty, tz, 0.0)
    for obj in movers.values():
        obj.hide_render = True
    for name, x, y, depth, scale, _speed, angle in GATE_MOVERS:
        src = movers[name]
        obj = bpy.data.objects.new(f"{name}_preview", src.data)
        bpy.context.scene.collection.objects.link(obj)
        place_gate_mover(obj, x, y, depth, scale, angle)
    import_title_door()
    if save_path is not None:
        save_path.parent.mkdir(parents=True, exist_ok=True)
        bpy.ops.wm.save_as_mainfile(filepath=str(save_path.resolve()))
    if preview_dir is None and ibl_path is None:
        return
    setup_lighting(tex.get("floor"))

    if ibl_path is not None:
        render_ibl(ibl_path)

    if preview_dir is not None:
        if "--with-mech" in argv:
            import_mech()
        preview_dir.mkdir(parents=True, exist_ok=True)
        setup_render(1280, 720, 48)
        scene = bpy.context.scene
        scene.render.image_settings.file_format = "PNG"
        scene.render.image_settings.color_depth = "8"
        views = {
            # 待機の視点（足場の上）
            "wait": ((0.0, 12.2, 6.6), (0.0, 12.5, 0.0), 60.0),
            # 足場の先から +Z の時計の門（ステージ選択の背景・降りるときに見える側）
            "gate": ((0.0, 12.4, 4.2), (0.0, 17.0, 20.0), 70.0),
            # 床から門と大階段（床の高さの輪 D より上・内側から）
            "floor": ((-7.0, 3.6, -6.0), (2.0, 13.0, 19.0), 80.0),
            # 踊り場から機体の後ろ（−Z）の大窓
            "back": ((0.0, 12.2, 17.0), (0.0, 9.0, -20.0), 70.0),
        }
        only = argv[argv.index("--only") + 1].split(",") if "--only" in argv else list(views)
        for name in only:
            eye, target, fov = views[name]
            set_camera(eye, target, fov)
            render(preview_dir / f"temple_{name}.png")


# gen_catapult.py が形状・テクスチャの関数を import して使い回す
if __name__ == "__main__":
    main()
