"""タイトル画面の扉（両開き・奥開き）を Blender で手続き生成するスクリプト。

使い方（Project/ で実行）:
    blender -b --factory-startup -P tools/BlenderPipeline/gen_title_door.py -- [オプション]
        --save <パス.blend>   生成した扉を .blend で保存（Blender で開いて確認する用）
        --export <フォルダ>   部位ごとに glTF を書き出す（例: Assets/Models/TitleDoor）
        --export-lines <フォルダ>  演出の線画用に、部位ごとの特徴線を細い角柱にした glTF だけを書き出す
                              （door_frame_lines など。扉本体とテクスチャは書き出さない）
        --export-light <フォルダ>  演出の「光の部屋」door_light（扉の奥の、開口部の形をした内向きの筒）だけを書き出す
        --preview <フォルダ>  確認用の PNG を3枚レンダリング（正面・斜め・開いた状態）

座標（Blender）: X=右 / Y=奥（正面は -Y 側から見る）/ Z=上。
部位:
    door_frame   … 枠＋台座。原点＝台座の底面の中央
    door_leaf_L  … 左の扉板。原点＝蝶番の軸（外側の縁 × 枠の奥側の面）
    door_leaf_R  … 右の扉板。同上
マテリアル: 0=Marble（本体） / 1=Gold（装飾）
寸法は 5_TitleScene.md §10.5 に合わせる。
"""

import math
import sys
from pathlib import Path

import bmesh
import bpy
import numpy as np
from mathutils import Matrix, Vector

# ============================================================
# パラメータ（単位 m）
# ============================================================
OPEN_HALF_W = 0.9        # 開口部の半幅（全幅 1.8）
SPRING_H = 2.0           # アーチの起点（台座上面から）
ARCH_RISE = 1.2          # 起点からアーチ頂点まで（頂点＝3.2）
FRAME_W = 0.3            # 枠の幅
FRAME_FRONT = -0.2       # 枠の手前の面（Y）
FRAME_BACK = 0.2         # 枠の奥の面（Y）。扉板の蝶番はここ
PEDESTAL_H = 0.15        # 台座の高さ
LEAF_T = 0.07            # 扉板の厚み
LEAF_GAP = 0.004         # 合わせ目の隙間（片側）
ARC_SEGMENTS = 16        # アーチ片側の分割数

# 線画（タイトルの演出 ③ で扉より先に現れる線）
LINE_ANGLE_DEG = 30.0    # 隣り合う面がこの角度以上折れている辺を線にする（アーチの分割は拾わない）
LINE_HALF_W = 0.012      # 線（角柱）の半分の太さ

# 光の部屋（タイトルの演出 ⑥ で扉が開いた奥に見える、ライティング無しの白い筒）
LIGHT_ROOM_MARGIN = 0.15  # 開口部より外へ広げる幅（枠の幅 0.3 の内側に収め、正面から枠の外へはみ出さない）
LIGHT_ROOM_DEPTH = 2.5    # 枠の奥の面からの奥行き
LIGHT_ROOM_FLOOR = 0.003  # 床を台座の上面から浮かせる（同じ高さだと深度が競合する）

MAT_MARBLE = 0
MAT_GOLD = 1

# 質感
UV_TILE_M = 2.0          # この長さ [m] でテクスチャ1枚（ボックス投影）
TEX_SIZE = 1024
MARBLE_SEED = 7
MARBLE_BASE = (0.93, 0.92, 0.89)   # 地の色（sRGB）
MARBLE_VEIN = (0.62, 0.64, 0.68)   # 筋の色（sRGB）
MARBLE_VEIN_STRENGTH = 0.45        # 筋の濃さ（0〜1）
MARBLE_VEIN_SHARPNESS = 4.0        # 大きいほど細く鋭い筋（10 を超えるとひび割れに見える）
MARBLE_BUMP = 0.25                 # 法線マップの強さ（磨いた石なのでごく弱く）
TEX_NAMES = {
    "marble_base": "Marble_BaseColor",
    "marble_normal": "Marble_NormalMap",  # "NormalMap" を含める＝cook が線形で圧縮する
    "gold_normal": "Gold_NormalMap",
}

# 尖頭アーチ：右側の弧の中心を左へ C だけずらす（半径 R = 半幅 + C）
ARC_C = (ARCH_RISE ** 2 - OPEN_HALF_W ** 2) / (2.0 * OPEN_HALF_W)
ARC_R = OPEN_HALF_W + ARC_C
Z0 = PEDESTAL_H


# ============================================================
# 形状ヘルパー
# ============================================================
class MeshBuilder:
    """頂点と面（マテリアル番号つき）を溜めて最後に1オブジェクトにする"""

    def __init__(self):
        self.verts = []
        self.faces = []
        self.mats = []

    def add_solid(self, verts, faces, mat):
        base = len(self.verts)
        self.verts.extend(verts)
        for f in faces:
            self.faces.append([base + i for i in f])
            self.mats.append(mat)

    def box(self, x0, x1, y0, y1, z0, z1, mat):
        v = [(x0, y0, z0), (x1, y0, z0), (x1, y1, z0), (x0, y1, z0),
             (x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1)]
        f = [(0, 3, 2, 1), (4, 5, 6, 7), (0, 1, 5, 4),
             (1, 2, 6, 5), (2, 3, 7, 6), (3, 0, 4, 7)]
        self.add_solid(v, f, mat)

    def prism(self, poly_xz, y0, y1, mat):
        """XZ 平面の凸多角形を Y 方向に押し出す"""
        n = len(poly_xz)
        v = [(x, y0, z) for x, z in poly_xz] + [(x, y1, z) for x, z in poly_xz]
        f = [list(range(n)), list(range(2 * n - 1, n - 1, -1))]
        for i in range(n):
            j = (i + 1) % n
            f.append((i, j, n + j, n + i))
        self.add_solid(v, f, mat)

    def band(self, outer, inner, y0, y1, mat, closed):
        """同じ点数の2本の線（外側・内側）の間を埋めて Y 方向に厚みを付ける"""
        n = len(outer)
        assert n == len(inner)
        # 頂点: 外前, 内前, 外奥, 内奥
        v = ([(x, y0, z) for x, z in outer] + [(x, y0, z) for x, z in inner]
             + [(x, y1, z) for x, z in outer] + [(x, y1, z) for x, z in inner])
        of, inf, ob, inb = 0, n, 2 * n, 3 * n
        f = []
        segs = n if closed else n - 1
        for i in range(segs):
            j = (i + 1) % n
            f.append((of + i, of + j, inf + j, inf + i))   # 前
            f.append((ob + i, inb + i, inb + j, ob + j))   # 奥
            f.append((of + i, ob + i, ob + j, of + j))     # 外壁
            f.append((inf + i, inf + j, inb + j, inb + i))  # 内壁
        if not closed:
            for k in (0, n - 1):  # 両端のふた
                f.append((of + k, inf + k, inb + k, ob + k))
        self.add_solid(v, f, mat)

    def build(self, name, materials):
        mesh = bpy.data.meshes.new(name)
        mesh.from_pydata(self.verts, [], self.faces)
        for poly, m in zip(mesh.polygons, self.mats):
            poly.material_index = m
        mesh.update()
        # 面の向きは各ソリッドごとに外向きへ揃える（巻き順を手で管理しない）
        bm = bmesh.new()
        bm.from_mesh(mesh)
        bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=1e-5)
        bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
        box_project_uv(bm)
        bm.to_mesh(mesh)
        bm.free()
        obj = bpy.data.objects.new(name, mesh)
        for mat in materials:
            obj.data.materials.append(mat)
        bpy.context.scene.collection.objects.link(obj)
        return obj


def box_project_uv(bm):
    """面ごとに法線が最も近い軸の方向から投影して、位置を UV にする。
    大理石は継ぎ目の無いタイルなので、面の境目の継ぎ目は模様に紛れる"""
    uv = bm.loops.layers.uv.verify()
    for face in bm.faces:
        n = face.normal
        ax = max(range(3), key=lambda i: abs(n[i]))
        u_axis, v_axis = [(1, 2), (0, 2), (0, 1)][ax]
        for loop in face.loops:
            co = loop.vert.co
            loop[uv].uv = (co[u_axis] / UV_TILE_M, co[v_axis] / UV_TILE_M)


def arch_x_at(d, x):
    """オフセット d のアーチ（右側の弧）の、X=x での高さ"""
    r = ARC_R + d
    return Z0 + SPRING_H + math.sqrt(max(r * r - (x + ARC_C) ** 2, 0.0))


def opening_profile(d):
    """開口部から外へ d 離れた U 字の線。右下 → アーチ → 左下（点数は d によらず一定）"""
    r = ARC_R + d
    phi1 = math.acos(ARC_C / r)  # 頂点（x=0）の角度
    right = [(OPEN_HALF_W + d, Z0)]
    for i in range(ARC_SEGMENTS + 1):
        phi = phi1 * i / ARC_SEGMENTS
        right.append((-ARC_C + r * math.cos(phi), Z0 + SPRING_H + r * math.sin(phi)))
    left = [(-x, z) for x, z in reversed(right[:-1])]
    return right + left


def leaf_outline(inset):
    """右の扉板の外形（ワールド座標）。inset で内側へ縮める。点数一定・凸"""
    xc = LEAF_GAP + inset           # 合わせ目側
    xh = OPEN_HALF_W - inset        # 蝶番側
    zb = Z0 + inset
    r = ARC_R - inset
    phi_top = math.acos(min((ARC_C + xc) / r, 1.0))
    pts = [(xc, zb), (xh, zb)]
    for i in range(ARC_SEGMENTS + 1):
        phi = phi_top * i / ARC_SEGMENTS
        pts.append((-ARC_C + r * math.cos(phi), Z0 + SPRING_H + r * math.sin(phi)))
    return pts


def mirror_x(poly):
    return [(-x, z) for x, z in reversed(poly)]


# ============================================================
# 部位
# ============================================================
def build_frame(mats):
    mb = MeshBuilder()
    hw = OPEN_HALF_W
    ow = hw + FRAME_W

    # 台座
    mb.box(-1.4, 1.4, -0.75, 0.45, 0.0, PEDESTAL_H, MAT_MARBLE)

    # 枠の本体（U字の帯）
    mb.band(opening_profile(FRAME_W), opening_profile(0.0), FRAME_FRONT, FRAME_BACK, MAT_MARBLE, closed=False)
    # 手前に盛り上がった縁取り（大理石）
    mb.band(opening_profile(0.22), opening_profile(0.10), FRAME_FRONT - 0.03, FRAME_FRONT, MAT_MARBLE, closed=False)
    # 金：内側の縁取り・外側の細い線
    mb.band(opening_profile(0.045), opening_profile(0.015), FRAME_FRONT - 0.015, FRAME_FRONT, MAT_GOLD, closed=False)
    mb.band(opening_profile(0.275), opening_profile(0.255), FRAME_FRONT - 0.012, FRAME_FRONT, MAT_GOLD, closed=False)

    for s in (1.0, -1.0):
        # 足元の台（大理石）
        x0, x1 = sorted((s * (hw - 0.02), s * (ow + 0.06)))
        mb.box(x0, x1, FRAME_FRONT - 0.07, FRAME_BACK + 0.04, Z0, Z0 + 0.3, MAT_MARBLE)
        # 金具（原神の足元の金色の部分）
        bracket = [(hw + 0.08, Z0), (ow + 0.06, Z0), (ow + 0.06, Z0 + 0.34),
                   (ow - 0.02, Z0 + 0.26), (hw + 0.16, Z0 + 0.08)]
        if s < 0:
            bracket = mirror_x(bracket)
        mb.prism(bracket, FRAME_FRONT - 0.09, FRAME_FRONT - 0.07, MAT_GOLD)
        # アーチ起点の張り出し（インポスト）＋上面の金の線
        x0, x1 = sorted((s * (ow - 0.06), s * (ow + 0.07)))
        mb.box(x0, x1, FRAME_FRONT - 0.06, FRAME_BACK + 0.03, Z0 + SPRING_H - 0.14, Z0 + SPRING_H, MAT_MARBLE)
        mb.box(x0, x1, FRAME_FRONT - 0.07, FRAME_BACK + 0.04, Z0 + SPRING_H, Z0 + SPRING_H + 0.02, MAT_GOLD)

    # 要石（アーチ頂点。上が広い台形）
    apex_in = Z0 + SPRING_H + ARCH_RISE
    apex_out = arch_x_at(FRAME_W, 0.0)
    key = [(-0.09, apex_in - 0.04), (0.09, apex_in - 0.04), (0.15, apex_out + 0.06), (-0.15, apex_out + 0.06)]
    mb.prism(key, FRAME_FRONT - 0.06, FRAME_BACK + 0.03, MAT_MARBLE)
    # 要石の金の菱形
    zc = (apex_in + apex_out) * 0.5 + 0.02
    mb.prism([(0.0, zc - 0.13), (0.07, zc), (0.0, zc + 0.13), (-0.07, zc)],
             FRAME_FRONT - 0.08, FRAME_FRONT - 0.06, MAT_GOLD)
    # 頂部の飾り（金）
    zt = apex_out + 0.06
    mb.prism([(-0.05, zt), (0.05, zt), (0.08, zt + 0.12), (0.0, zt + 0.28), (-0.08, zt + 0.12)],
             -0.03, 0.03, MAT_GOLD)

    return mb.build("door_frame", mats)


def build_leaf_right(mats):
    """右の扉板をワールド座標で作り、最後に原点を蝶番へ移す"""
    mb = MeshBuilder()
    y_back = FRAME_BACK
    y_front = FRAME_BACK - LEAF_T

    # 扉板の本体
    mb.prism(leaf_outline(0.0), y_front, y_back, MAT_MARBLE)
    # 金の縁（鏡板の外周）
    mb.band(leaf_outline(0.07), leaf_outline(0.095), y_front - 0.012, y_front, MAT_GOLD, closed=True)
    # 盛り上がった鏡板（大理石）
    mb.prism(leaf_outline(0.12), y_front - 0.02, y_front, MAT_MARBLE)

    # 中央の紋章（左右の扉板にまたがる円環の半分＋菱形の半分）
    zc = Z0 + 1.65
    g = LEAF_GAP
    ring_out, ring_in = [], []
    n = 12
    for i in range(n + 1):
        a = -math.pi / 2 + math.pi * i / n
        ring_out.append((g + 0.24 * math.cos(a), zc + 0.24 * math.sin(a)))
        ring_in.append((g + 0.19 * math.cos(a), zc + 0.19 * math.sin(a)))
    mb.band(ring_out, ring_in, y_front - 0.035, y_front - 0.02, MAT_GOLD, closed=False)
    mb.prism([(g, zc - 0.15), (g + 0.09, zc), (g, zc + 0.15)], y_front - 0.04, y_front - 0.02, MAT_GOLD)
    # 紋章から上下へ伸びる細い金の線
    mb.box(g, g + 0.012, y_front - 0.03, y_front - 0.02, Z0 + 0.25, zc - 0.24, MAT_GOLD)
    mb.box(g, g + 0.012, y_front - 0.03, y_front - 0.02, zc + 0.24, arch_x_at(-0.14, g + 0.012), MAT_GOLD)

    obj = mb.build("door_leaf_R", mats)
    hinge = Vector((OPEN_HALF_W, y_back, 0.0))
    obj.data.transform(Matrix.Translation(-hinge))
    obj.location = hinge
    return obj


def build_leaf_left(right_obj, mats):
    mesh = right_obj.data.copy()
    mesh.name = "door_leaf_L"
    mesh.transform(Matrix.Scale(-1.0, 4, (1.0, 0.0, 0.0)))
    bm = bmesh.new()
    bm.from_mesh(mesh)
    bmesh.ops.reverse_faces(bm, faces=bm.faces)  # 鏡映で裏返った面を戻す
    bm.to_mesh(mesh)
    bm.free()
    obj = bpy.data.objects.new("door_leaf_L", mesh)
    bpy.context.scene.collection.objects.link(obj)
    obj.location = (-OPEN_HALF_W, FRAME_BACK, 0.0)
    return obj


# ============================================================
# テクスチャ（numpy で手続き生成。すべて継ぎ目の無いタイル）
# 配列は行 0 = 画像の上（gen_terrain_material.py と同じ規約。法線は ny = -dy）
# ============================================================
def tileable_noise(rng, n, beta):
    """白色ノイズを周波数空間で 1/f^beta に整形する。FFT なので自動的に周期的＝タイルできる"""
    white = rng.standard_normal((n, n))
    fx = np.fft.fftfreq(n)[None, :]
    fy = np.fft.fftfreq(n)[:, None]
    r = np.sqrt(fx * fx + fy * fy)
    r[0, 0] = 1.0
    spec = np.fft.fft2(white) / (r ** beta)
    spec[0, 0] = 0.0
    out = np.real(np.fft.ifft2(spec))
    return (out - out.min()) / (out.max() - out.min())


def marble_fields(n):
    """(ベースカラー sRGB, 高さ) を返す"""
    rng = np.random.default_rng(MARBLE_SEED)
    warp = tileable_noise(rng, n, 2.2)    # 筋をうねらせる大きな揺らぎ
    warp2 = tileable_noise(rng, n, 2.0)
    cloud = tileable_noise(rng, n, 1.7)   # 地のむら
    y, x = np.mgrid[0:n, 0:n] / n

    def veins(kx, ky, w, field, sharp):
        # 整数周波数の縞を揺らす（整数なのでタイルが切れない）。|sin| が 0 の所が細い筋
        phase = 2.0 * np.pi * (kx * x + ky * y) + w * 2.0 * np.pi * field
        return (1.0 - np.abs(np.sin(phase))) ** sharp

    # 筋は太く柔らかく（鋭くするとひび割れに見える）。地の雲状のむらと重ねて模様にする
    main = veins(1, 2, 1.6, warp, MARBLE_VEIN_SHARPNESS)
    sub = veins(3, -1, 1.1, warp2, MARBLE_VEIN_SHARPNESS * 1.8) * 0.35
    v = np.clip(main + sub, 0.0, 1.0)
    haze = np.clip((cloud - 0.45) * 1.6, 0.0, 1.0) * 0.25   # 筋の周りのうっすらした灰色のもや

    base = np.array(MARBLE_BASE)[None, None, :] + (cloud[..., None] - 0.5) * 0.04
    vein = np.array(MARBLE_VEIN)[None, None, :]
    mix = np.clip(v * MARBLE_VEIN_STRENGTH + haze, 0.0, 1.0)[..., None]
    color = base * (1.0 - mix) + vein * mix
    height = -v * 0.3 + (cloud - 0.5) * 0.1   # 磨いた石なので溝はほとんど付けない
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
    print(f"[gen_title_door] texture {path}")
    return img


def make_textures(out_dir):
    out_dir.mkdir(parents=True, exist_ok=True)
    color, height = marble_fields(TEX_SIZE)
    flat = np.tile(np.array([0.5, 0.5, 1.0]), (4, 4, 1))
    return {
        "marble_base": save_png(TEX_NAMES["marble_base"], color, out_dir, non_color=False),
        "marble_normal": save_png(TEX_NAMES["marble_normal"], height_to_normal(height, MARBLE_BUMP), out_dir, non_color=True),
        "gold_normal": save_png(TEX_NAMES["gold_normal"], flat, out_dir, non_color=True),
    }


# ============================================================
# マテリアル（glTF にそのまま出る構成：画像 → Base Color / Normal Map）
# ============================================================
def attach_normal_map(nt, bsdf, image):
    tex = nt.nodes.new("ShaderNodeTexImage")
    tex.image = image
    nmap = nt.nodes.new("ShaderNodeNormalMap")
    nt.links.new(tex.outputs["Color"], nmap.inputs["Color"])
    nt.links.new(nmap.outputs["Normal"], bsdf.inputs["Normal"])


def make_materials(textures):
    marble = bpy.data.materials.new("Marble")
    marble.use_nodes = True
    nt = marble.node_tree
    bsdf = nt.nodes["Principled BSDF"]
    bsdf.inputs["Roughness"].default_value = 0.35
    base = nt.nodes.new("ShaderNodeTexImage")
    base.image = textures["marble_base"]
    nt.links.new(base.outputs["Color"], bsdf.inputs["Base Color"])
    attach_normal_map(nt, bsdf, textures["marble_normal"])

    gold = bpy.data.materials.new("Gold")
    gold.use_nodes = True
    nt = gold.node_tree
    g = nt.nodes["Principled BSDF"]
    g.inputs["Base Color"].default_value = (1.0, 0.68, 0.24, 1.0)
    g.inputs["Metallic"].default_value = 1.0
    g.inputs["Roughness"].default_value = 0.25
    # 平らな法線マップ。cook の PBR 切替は法線マップの有無だけで決まるので金にも付ける
    attach_normal_map(nt, g, textures["gold_normal"])
    return [marble, gold]


# ============================================================
# プレビュー
# ============================================================
def setup_world(strength):
    world = bpy.data.worlds.new("Sky")
    bpy.context.scene.world = world
    world.use_nodes = True
    nt = world.node_tree
    bg = nt.nodes["Background"]
    sky = nt.nodes.new("ShaderNodeTexSky")
    sky.sky_type = "NISHITA"
    sky.sun_elevation = math.radians(35.0)
    sky.sun_rotation = math.radians(200.0)
    nt.links.new(sky.outputs["Color"], bg.inputs["Color"])
    bg.inputs["Strength"].default_value = strength


def add_water():
    bpy.ops.mesh.primitive_plane_add(size=60.0, location=(0.0, 0.0, 0.1))
    water = bpy.context.active_object
    mat = bpy.data.materials.new("WaterPreview")
    mat.use_nodes = True
    b = mat.node_tree.nodes["Principled BSDF"]
    b.inputs["Base Color"].default_value = (0.05, 0.22, 0.32, 1.0)
    b.inputs["Roughness"].default_value = 0.03
    water.data.materials.append(mat)


def add_sun(energy):
    sun = bpy.data.lights.new("Sun", "SUN")
    sun.energy = energy
    obj = bpy.data.objects.new("Sun", sun)
    obj.rotation_euler = (math.radians(50.0), 0.0, math.radians(-30.0))
    bpy.context.scene.collection.objects.link(obj)
    return obj


def add_inner_light():
    """扉の奥の光（光る板＋ポイントライト）。エンジンでも同じ構成にする"""
    # 開口部と同じアーチ形を大きくして奥に置く（遠いぶん透視で小さく見える）（正面から見て縁の隙間が出ないように）
    glow = opening_profile(0.7)
    glow = [(x, z - Z0 + 0.1) for x, z in glow]  # 足元は水面の高さまで下げる
    mb = MeshBuilder()
    mb.prism(glow, FRAME_BACK + 1.2, FRAME_BACK + 1.21, 0)
    plane = mb.build("InnerGlow", [])
    mat = bpy.data.materials.new("InnerGlow")
    mat.use_nodes = True
    nt = mat.node_tree
    nt.nodes.remove(nt.nodes["Principled BSDF"])
    em = nt.nodes.new("ShaderNodeEmission")
    em.inputs["Color"].default_value = (1.0, 0.95, 0.85, 1.0)
    em.inputs["Strength"].default_value = 25.0
    nt.links.new(em.outputs["Emission"], nt.nodes["Material Output"].inputs["Surface"])
    plane.data.materials.append(mat)

    light = bpy.data.lights.new("InnerPoint", "POINT")
    light.energy = 1500.0
    light.color = (1.0, 0.9, 0.75)
    light.shadow_soft_size = 0.5
    obj = bpy.data.objects.new("InnerPoint", light)
    obj.location = (0.0, FRAME_BACK + 0.5, Z0 + 1.8)
    bpy.context.scene.collection.objects.link(obj)


def set_camera(loc, target, lens=35.0):
    cam = bpy.context.scene.camera
    if cam is None:
        cam = bpy.data.objects.new("Camera", bpy.data.cameras.new("Camera"))
        bpy.context.scene.collection.objects.link(cam)
        bpy.context.scene.camera = cam
    cam.data.lens = lens
    cam.location = loc
    direction = Vector(target) - Vector(loc)
    cam.rotation_euler = direction.to_track_quat("-Z", "Y").to_euler()


def render(path):
    bpy.context.scene.render.filepath = str(path)
    bpy.ops.render.render(write_still=True)
    print(f"[gen_title_door] wrote {path}")


def setup_render():
    scene = bpy.context.scene
    scene.render.engine = "CYCLES"
    scene.cycles.device = "CPU"
    scene.cycles.samples = 48
    scene.cycles.use_denoising = True
    scene.render.resolution_x = 1280
    scene.render.resolution_y = 960
    scene.view_settings.view_transform = "AgX"
    scene.view_settings.look = "AgX - Medium High Contrast"
    scene.view_settings.exposure = -1.2


def report_stats(objs):
    for obj in objs:
        tris = sum(len(p.vertices) - 2 for p in obj.data.polygons)
        gold = sum(1 for p in obj.data.polygons if p.material_index == MAT_GOLD)
        print(f"[gen_title_door] {obj.name}: {tris} tris, gold faces {gold}/{len(obj.data.polygons)}")


def save_blend(path):
    path.parent.mkdir(parents=True, exist_ok=True)
    bpy.ops.wm.save_as_mainfile(filepath=str(path.resolve()))
    print(f"[gen_title_door] saved {path}")


def export_parts(out_dir, objs):
    """部位ごとに別の glTF へ書き出す。
    cook はノードのワールド変換を頂点へ焼き込むので、書き出す間だけ位置・回転を 0 に戻す
    （扉板の原点＝蝶番の軸を保つため）"""
    out_dir.mkdir(parents=True, exist_ok=True)
    for obj in objs:
        saved = (obj.location.copy(), obj.rotation_euler.copy())
        obj.location = (0.0, 0.0, 0.0)
        obj.rotation_euler = (0.0, 0.0, 0.0)
        bpy.ops.object.select_all(action="DESELECT")
        obj.select_set(True)
        bpy.context.view_layer.objects.active = obj
        path = out_dir / f"{obj.name}.gltf"
        bpy.ops.export_scene.gltf(filepath=str(path.resolve()), export_format="GLTF_SEPARATE",
                                  use_selection=True, export_yup=True, export_apply=True,
                                  export_materials="EXPORT")
        obj.location, obj.rotation_euler = saved
        print(f"[gen_title_door] exported {path}")


def build_feature_lines(obj):
    """obj の特徴線（折れ目・縁・大理石と金の境目）を細い角柱のメッシュにした別オブジェクトを作る。
    メッシュは obj と同じローカル空間で作り、位置・回転も obj に合わせる（エンジン側で同じ変換を使える）"""
    src = bmesh.new()
    src.from_mesh(obj.data)
    threshold = math.radians(LINE_ANGLE_DEG)
    segments = []
    for e in src.edges:
        faces = e.link_faces
        if len(faces) == 2:
            is_crease = e.calc_face_angle(0.0) >= threshold
            is_seam = faces[0].material_index != faces[1].material_index
            if not (is_crease or is_seam):
                continue
        elif len(faces) != 1:
            continue
        a, b = e.verts[0].co.copy(), e.verts[1].co.copy()
        if (b - a).length > 1e-4:
            segments.append((a, b))
    src.free()

    r = LINE_HALF_W
    bm = bmesh.new()
    for a, b in segments:
        d = (b - a).normalized()
        u = d.cross(Vector((0.0, 0.0, 1.0)))
        if u.length < 1e-3:
            u = d.cross(Vector((1.0, 0.0, 0.0)))
        u.normalize()
        v = d.cross(u)
        # 角でつながるよう、両端を太さぶん延ばす
        a0, b0 = a - d * r, b + d * r
        offs = [(u * math.cos(t) + v * math.sin(t)) * r * math.sqrt(2.0)
                for t in (math.pi * (0.25 + 0.5 * k) for k in range(4))]
        ring_a = [bm.verts.new(a0 + o) for o in offs]
        ring_b = [bm.verts.new(b0 + o) for o in offs]
        for k in range(4):
            j = (k + 1) % 4
            bm.faces.new((ring_a[k], ring_a[j], ring_b[j], ring_b[k]))
        bm.faces.new(list(reversed(ring_a)))
        bm.faces.new(ring_b)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)

    name = f"{obj.name}_lines"
    mesh = bpy.data.meshes.new(name)
    bm.to_mesh(mesh)
    bm.free()
    mat = bpy.data.materials.get("DoorLine") or bpy.data.materials.new("DoorLine")
    mesh.materials.append(mat)
    line_obj = bpy.data.objects.new(name, mesh)
    bpy.context.scene.collection.objects.link(line_obj)
    line_obj.location = obj.location.copy()
    line_obj.rotation_euler = obj.rotation_euler.copy()
    print(f"[gen_title_door] {name}: {len(segments)} lines, {len(mesh.polygons) * 2} tris")
    return line_obj


def build_light_room():
    """開口部の形を奥へ押し出した筒（手前は開放、奥は閉じる）。面は内向きにし、外からは見えないようにする。
    枠の奥の面から始めるので枠との間に隙間が無く、開口部から覗いても背景が見えない"""
    profile = opening_profile(LIGHT_ROOM_MARGIN)          # 右下 → アーチ → 左下
    z_floor = Z0 + LIGHT_ROOM_FLOOR
    profile = [(x, max(z, z_floor)) for x, z in profile]
    y0, y1 = FRAME_BACK, FRAME_BACK + LIGHT_ROOM_DEPTH

    bm = bmesh.new()
    front = [bm.verts.new((x, y0, z)) for x, z in profile]
    back = [bm.verts.new((x, y1, z)) for x, z in profile]
    n = len(profile)
    walls = []
    for i in range(n):  # 最後の辺（左下 → 右下）が床
        j = (i + 1) % n
        walls.append(bm.faces.new((front[i], front[j], back[j], back[i])))
    cap = bm.faces.new(back)

    # 内向きにそろえる（筒の中心から見て面が手前を向くように）
    center = Vector((0.0, (y0 + y1) * 0.5, Z0 + (SPRING_H + ARCH_RISE) * 0.5))
    bm.normal_update()
    for f in walls + [cap]:
        if f.normal.dot(center - f.calc_center_median()) < 0.0:
            f.normal_flip()

    mesh = bpy.data.meshes.new("door_light")
    bm.to_mesh(mesh)
    bm.free()
    mesh.materials.append(bpy.data.materials.get("DoorLight") or bpy.data.materials.new("DoorLight"))
    obj = bpy.data.objects.new("door_light", mesh)
    bpy.context.scene.collection.objects.link(obj)
    print(f"[gen_title_door] door_light: {len(mesh.polygons)} faces")
    return obj


def arg_path(argv, name):
    return Path(argv[argv.index(name) + 1]) if name in argv else None


def main():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    preview_dir = arg_path(argv, "--preview")
    save_path = arg_path(argv, "--save")
    export_dir = arg_path(argv, "--export")
    lines_dir = arg_path(argv, "--export-lines")
    light_dir = arg_path(argv, "--export-light")

    bpy.ops.wm.read_factory_settings(use_empty=True)
    # テクスチャは書き出し先に置く（glTF からの参照がそのまま cook で解決される）。書き出さない時は一時フォルダ
    tex_dir = export_dir if export_dir is not None else Path(bpy.app.tempdir) / "title_door_tex"
    mats = make_materials(make_textures(tex_dir))
    frame = build_frame(mats)
    leaf_r = build_leaf_right(mats)
    leaf_l = build_leaf_left(leaf_r, mats)
    report_stats([frame, leaf_l, leaf_r])

    # 保存と書き出しはプレビュー用の水面・光・開いた扉を足す前に行う
    if save_path is not None:
        save_blend(save_path)
    if export_dir is not None:
        export_parts(export_dir, [frame, leaf_l, leaf_r])
    if lines_dir is not None:
        lines = [build_feature_lines(o) for o in (frame, leaf_l, leaf_r)]
        export_parts(lines_dir, lines)
        for o in lines:
            bpy.data.objects.remove(o)
    if light_dir is not None:
        room = build_light_room()
        export_parts(light_dir, [room])
        bpy.data.objects.remove(room)

    if preview_dir is None:
        return
    preview_dir.mkdir(parents=True, exist_ok=True)
    setup_render()
    setup_world(0.6)
    add_water()
    sun = add_sun(4.0)

    # 1) 正面（閉）
    set_camera((0.0, -7.5, 1.9), (0.0, 0.0, 1.9))
    render(preview_dir / "door_front.png")
    # 2) 斜め（閉）
    set_camera((4.2, -5.5, 1.4), (0.0, 0.0, 1.9))
    render(preview_dir / "door_angle.png")
    # 3) 開いた状態（奥へ 80°）＋中の光、周りを暗く
    open_deg = 80.0
    leaf_r.rotation_euler.z = math.radians(-open_deg)
    leaf_l.rotation_euler.z = math.radians(open_deg)
    add_inner_light()
    bpy.context.scene.world.node_tree.nodes["Background"].inputs["Strength"].default_value = 0.25
    sun.data.energy = 1.0
    set_camera((0.0, -6.0, 1.6), (0.0, 0.0, 1.9))
    render(preview_dir / "door_open.png")


main()
