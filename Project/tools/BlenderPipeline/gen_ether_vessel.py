"""エーテルの器（ゲームオーバー画面の主役。19_GameOverAndDifficulty.md §4）を組み立てるスクリプト。

使い方（Project/ で実行）:
    blender -b --factory-startup -P tools/BlenderPipeline/gen_ether_vessel.py -- [オプション]
        --export <フォルダ>    部品ごとの glTF とテクスチャ(PNG)を書き出し、寸法の JSON を書く
                               （例: Assets/Models/EtherVessel/Export → cook で Resources へ。
                                JSON は Resources/Json/Tuning/EtherVesselGeometry.json）
        --preview <フォルダ>   正面・横・35° 見下ろし・傾け（I / IX）・北極の金具の PNG
        --save <パス.blend>    組み立てた結果を .blend で保存

Tripo の部品 5 つ（Assets/Models/EtherVessel/EtherVessel_*.glb。どれもメッシュ 1・マテリアル 1・画像 3 枚、
長い辺が約 1m に正規化済み）を読み、下から 土台本体(Table) → 支柱(Pillar) → 台座(RingStand、水平の輪) と積む。
文字盤(Dial)は台座の輪の穴に縦に通し、その中に球（ガラスと液体はエンジンが描く。ここでは作らない）。
球を半周抱える C の字の帯と極の金具はこのスクリプトで作り、針(Pointer)と一緒に Swing（原点＝球の中心）にまとめる。

座標（Blender）: X=右 / Y=奥（正面は -Y 側から見る）/ Z=上。土台の脚の下が Z=0。
cook は glTF の X だけを反転するので、エンジンでは (x, y, z) = (-X, Z, -Y)。部品はすべて Z 軸上に並ぶので
エンジンの位置は (0, Z, 0)、正面はエンジンの +Z、見る人の左（文字盤の I の側）はエンジンの +X。

書き出しは部品ごとの glTF（Separate）。cook がワールド変換を頂点へ焼くので、書き出す間だけ位置を 0 に戻す
（原点＝各部品の回転の軸。Blender の GUI から書き出さないこと）。
"""

import json
import math
import struct
import sys
import zlib
from pathlib import Path

import bmesh
import bpy
import numpy as np
from mathutils import Matrix, Vector

LOG = "[gen_ether_vessel]"
SRC_DIR = Path("Assets/Models/EtherVessel")
GEOMETRY_JSON = Path("Resources/Json/Tuning/EtherVesselGeometry.json")
RESOURCE_DIR = "Resources/Models/EtherVessel/Export"
PARTS = ("Table", "Pillar", "RingStand", "Dial", "Pointer")

# ============================================================
# Tripo の部品の寸法（正規化されたままの座標で測った値。2026-10-24）
# ============================================================
TABLE_TOP_Z = 0.683          # 天板の上面
PILLAR_FLANGE_Z = 0.30       # 支柱の歯車の下のつば（ここから下の軸は天板の穴に差す）
PILLAR_SEAT_Z = 0.86         # 台座の中央の小さな柱を乗せる高さ（柱頭）
RING_PLANE_Z = 0.485         # 台座の水平の輪の高さ（＝球の中心）
RING_INNER_R = 0.367         # 輪の穴の半径
DIAL_CENTER_Z = 0.488        # 文字盤の輪の中心
DIAL_OUTER_R = 0.49          # 文字盤の外径
DIAL_BAND_INNER_R = 0.385    # 数字の帯の内側の縁（針の先をここに届かせる）
DIAL_I_DEG = 45.0            # 真上から I・IX までの角度（I は見る人の左）。間に点が 3 つずつ＝9 目盛りで 11.25° おき（プレビューで測った）
DIAL_TICKS = 9
POINTER_H = 0.984            # 針の高さ
POINTER_BASE_R = 0.122       # 針の根元の円盤の半径（z 0〜0.039）
POINTER_BASE_H = 0.039

# ============================================================
# 組み立て（2026-10-24 ユーザーのスケッチ：台座の輪の中に文字盤と球）
# ============================================================
RING_SCALE = 2.0             # 台座の拡大（外径 0.98 → 1.96m）。全体の大きさはこれで決まる
TABLE_SCALE = 1.7
PILLAR_SCALE = 1.3
DIAL_OUTER_IN_RING = 0.35    # 文字盤の外径（台座の座標）。輪の穴（0.367）に当たらない大きさ。下の縁が台座の中央の柱の近くに来る
DIAL_BACK = 0.09             # 文字盤を奥（+Y）へ下げる量（m）。針が文字盤の帯に刺さらないように。輪の穴の内側に収まる上限は約 0.11
SPHERE_OF_DIAL = 0.40        # 球の半径 ÷ 文字盤の外径（スケッチの比率）
POINTER_REACH = 0.01         # 針の先を数字の帯の内側の縁からこれだけ（文字盤の座標）帯へ入れる

# C の字の帯と極の金具（球の半径に対する比）
BAND_GAP = 0.04              # 球の表面から帯の内側まで（ガラスに触れない）
BAND_THICK = 0.03            # 帯の厚み（半径方向）
BAND_WIDTH = 0.10            # 帯の幅（前後）
BAND_SEGMENTS = 64
CAP_R = 0.18                 # 極の金具（円錐台）の下の半径
CAP_TOP_R = 0.14
CAP_SINK = 0.03              # 金具の下の面をガラスの中へ沈める深さ（すき間を見せない）
CAP_LIP = 0.02               # 金具の上の面を帯の外側より出す高さ
# 北極の差し込み口（針の根元の円盤がはまる短い筒。針の座標＝針の拡大率に対する比）
SOCKET_INNER = 1.05          # 筒の内径 ÷ 針の根元の半径
SOCKET_OUTER = 1.30
SOCKET_DEPTH = 0.05          # 筒の高さ（針の座標）。根元の円盤（0.039）を隠す
SOUTH_KNOB_R = 0.06          # 南極の金具の先の小さな玉
SOUTH_KNOB_H = 0.06

# ============================================================
# 色（19 番 §4.3）。台座の Tripo のテクスチャはエナメルが紫・金が肌色なので、色相・彩度・明度を
# ほかの部品の値へ移す（中央値をそろえて、模様の濃淡は残す）
# ============================================================
ENAMEL_FROM = "Table"        # エナメルの目標＝土台本体の群青（中央値 h236・s0.60・v0.28）
GOLD_FROM = "Pointer"        # 金の目標＝針の金（中央値 h41・s0.56・v0.99）。C の字の帯もこの金
TEXTURE_SIZE = 2048

PREVIEW_RES = 1024


def log(msg):
    print(f"{LOG} {msg}")


def parse_args():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    opts = {}
    i = 0
    while i < len(argv):
        key = argv[i].lstrip("-")
        opts[key] = argv[i + 1] if i + 1 < len(argv) and not argv[i + 1].startswith("--") else True
        i += 2 if opts[key] is not True else 1
    return opts


# ============================================================
# 読み込みと材質の整理
# ============================================================
def import_part(name):
    before = set(bpy.data.objects)
    bpy.ops.import_scene.gltf(filepath=str(SRC_DIR / f"EtherVessel_{name}.glb"))
    new = [o for o in bpy.data.objects if o not in before]
    mesh = next(o for o in new if o.type == "MESH")
    for o in new:
        if o is not mesh:
            bpy.data.objects.remove(o)
    mesh.location = (0, 0, 0)      # Tripo の glb はノードに 1mm ほどのずれを持っている
    mesh.name = f"EtherVessel_{name}"
    mesh.data.name = mesh.name
    return mesh


def image_pixels(img):
    px = np.empty(img.size[0] * img.size[1] * img.channels, dtype=np.float32)
    img.pixels.foreach_get(px)
    return px.reshape(img.size[1], img.size[0], img.channels)


def tex_node_into(mat, socket_name):
    """BSDF の入力へつながる画像ノード（間の Separate Color / Normal Map をたどる）"""
    nodes = mat.node_tree.nodes
    bsdf = next(n for n in nodes if n.type == "BSDF_PRINCIPLED")
    link = bsdf.inputs[socket_name].links[0] if bsdf.inputs[socket_name].links else None
    node = link.from_node if link else None
    while node is not None and node.type != "TEX_IMAGE":
        ins = [i for i in node.inputs if i.links]
        node = ins[0].links[0].from_node if ins else None
    return bsdf, node


def prepare_material(obj, part):
    """材質と画像の名前をそろえ、金属度・粗さのテクスチャを係数（中央値）に置き換える。
    エンジンはこのテクスチャを読まず、cook は係数が無いと glTF 既定の 1.0 を .mat に入れるため（rig_hub_mech.py と同じ理由）"""
    mat = obj.data.materials[0]
    mat.name = f"EtherVessel_{part}"
    bsdf, base = tex_node_into(mat, "Base Color")
    _, normal = tex_node_into(mat, "Normal")
    _, mr = tex_node_into(mat, "Roughness")
    base.image.name = f"EtherVessel_{part}_BaseColor"
    normal.image.name = f"EtherVessel_{part}_NormalMap"
    px = image_pixels(mr.image)
    used = (px[..., 1] > 0.01) | (px[..., 2] > 0.01)
    rough = round(float(np.median(px[..., 1][used])), 3)
    metal = round(float(np.median(px[..., 2][used])), 3)
    for sock in ("Metallic", "Roughness"):
        for l in list(bsdf.inputs[sock].links):
            mat.node_tree.links.remove(l)
    bsdf.inputs["Metallic"].default_value = metal
    bsdf.inputs["Roughness"].default_value = rough
    img = mr.image
    for n in [n for n in mat.node_tree.nodes if n.type in ("SEPARATE_COLOR", "SEPRGB") or n == mr]:
        mat.node_tree.nodes.remove(n)
    bpy.data.images.remove(img)
    log(f"{part}: metallic={metal} roughness={rough}")
    return {"material": mat, "base": base.image, "normal": normal.image, "metallic": metal, "roughness": rough}


# ============================================================
# 台座の色直し（HSV の中央値をほかの部品へ移す）
# ============================================================
def rgb_to_hsv(rgb):
    r, g, b = rgb[..., 0], rgb[..., 1], rgb[..., 2]
    mx = rgb.max(-1)
    mn = rgb.min(-1)
    d = mx - mn
    safe = np.where(d > 1e-6, d, 1.0)
    h = np.where(mx == r, (g - b) / safe % 6.0, np.where(mx == g, (b - r) / safe + 2.0, (r - g) / safe + 4.0))
    h = np.where(d > 1e-6, h * 60.0, 0.0)
    s = np.where(mx > 1e-6, d / np.maximum(mx, 1e-6), 0.0)
    return np.stack([h % 360.0, s, mx], -1)


def hsv_to_rgb(hsv):
    h, s, v = hsv[..., 0] % 360.0, hsv[..., 1], hsv[..., 2]
    c = v * s
    x = c * (1.0 - np.abs((h / 60.0) % 2.0 - 1.0))
    m = v - c
    z = np.zeros_like(h)
    sector = (h // 60.0).astype(int) % 6
    table = [(c, x, z), (x, c, z), (z, c, x), (z, x, c), (x, z, c), (c, z, x)]
    out = np.zeros(h.shape + (3,), dtype=np.float32)
    for i, (rr, gg, bb) in enumerate(table):
        sel = sector == i
        out[sel] = np.stack([rr[sel], gg[sel], bb[sel]], -1)
    return out + m[..., None]


def smoothstep(e0, e1, x):
    t = np.clip((x - e0) / (e1 - e0), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def enamel_mask(hsv):
    """群青・紫のエナメル（暗い青〜紫）"""
    h, s, v = hsv[..., 0], hsv[..., 1], hsv[..., 2]
    return (h > 190) & (h < 345) & (s > 0.3) & (v < 0.7)


def gold_mask(hsv):
    h, s, v = hsv[..., 0], hsv[..., 1], hsv[..., 2]
    return (h >= 15) & (h < 65) & (v > 0.45)


def hsv_median(hsv, mask):
    return np.median(hsv[mask], axis=0)


def transfer(hsv, src_med, dst_med):
    """色相は差を足し、彩度・明度は比を掛ける（模様の濃淡を残す）"""
    out = hsv.copy()
    out[..., 0] = hsv[..., 0] + (dst_med[0] - src_med[0])
    out[..., 1] = np.clip(hsv[..., 1] * dst_med[1] / max(src_med[1], 1e-3), 0.0, 1.0)
    out[..., 2] = np.clip(hsv[..., 2] * dst_med[2] / max(src_med[2], 1e-3), 0.0, 1.0)
    return out


def recolor_ring_stand(info, ref):
    """台座：紫のエナメル → 土台本体の群青、肌色の金 → 針の金"""
    img = info["base"]
    px = image_pixels(img)
    hsv = rgb_to_hsv(px[..., :3])
    h, sat, v = hsv[..., 0], hsv[..., 1], hsv[..., 2]
    # 紫の色相（315° のまわり）で彩度のある所をエナメルとする。明るさで分けると縁の中間の明るさの紫が金の側へ回って
    # 赤紫のにじみが残る（2026-10-24 cook 後に確認）。肌色の金は色相 24° 前後なので外れる
    hue_dist = np.abs((h - 315.0 + 180.0) % 360.0 - 180.0)
    enamel_w = smoothstep(60.0, 40.0, hue_dist) * smoothstep(0.12, 0.25, sat)
    src_enamel = hsv_median(hsv, enamel_w > 0.99)
    src_gold = hsv_median(hsv, (enamel_w < 0.01) & (v > 0.85))
    ref_enamel = hsv_median(ref[ENAMEL_FROM], enamel_mask(ref[ENAMEL_FROM]))
    ref_gold = hsv_median(ref[GOLD_FROM], gold_mask(ref[GOLD_FROM]))
    enamel_rgb = hsv_to_rgb(transfer(hsv, src_enamel, ref_enamel))
    gold_rgb = hsv_to_rgb(transfer(hsv, src_gold, ref_gold))
    w = enamel_w[..., None]
    px[..., :3] = gold_rgb * (1.0 - w) + enamel_rgb * w
    img.pixels.foreach_set(px.ravel())
    img.update()
    log(f"RingStand recolor: enamel {np.round(src_enamel, 2)} -> {np.round(ref_enamel, 2)}, "
        f"gold {np.round(src_gold, 2)} -> {np.round(ref_gold, 2)}")


# ============================================================
# C の字の帯と極の金具（針の金の一点を UV で拾う）
# ============================================================
def find_gold_patch(info, block=16):
    """針のテクスチャで、金が平らに続く所（色のばらつきが小さく、法線が真上）の UV の中心"""
    base = image_pixels(info["base"])[..., :3]
    nrm = image_pixels(info["normal"])[..., :3]
    hb, wb = base.shape[0] // block, base.shape[1] // block
    b = base[:hb * block, :wb * block].reshape(hb, block, wb, block, 3)
    n = nrm[:hb * block, :wb * block].reshape(hb, block, wb, block, 3)
    mean = b.mean((1, 3))
    std = b.std((1, 3)).sum(-1)
    ndev = np.abs(n[..., 0] - 0.5).mean((1, 3)) + np.abs(n[..., 1] - 0.5).mean((1, 3))
    hsv = rgb_to_hsv(mean)
    ok = (hsv[..., 0] > 36) & (hsv[..., 0] < 46) & (hsv[..., 1] > 0.45) & (hsv[..., 1] < 0.65) & (hsv[..., 2] > 0.85)
    score = np.where(ok, std + ndev * 2.0, np.inf)
    y, x = np.unravel_index(np.argmin(score), score.shape)
    uv = ((x + 0.5) * block / base.shape[1], (y + 0.5) * block / base.shape[0])
    log(f"gold patch uv={np.round(uv, 4)} rgb={np.round(mean[y, x], 3)} std={std[y, x]:.4f} ndev={ndev[y, x]:.4f}")
    return Vector(uv), 0.5 * block / base.shape[1]


def add_ring_quads(bm, rings):
    for a, b in zip(rings[:-1], rings[1:]):
        k = len(a)
        for i in range(k):
            bm.faces.new((a[i], a[(i + 1) % k], b[(i + 1) % k], b[i]))


def add_lathe(bm, profile, segments=48, z_sign=1.0):
    """(半径, 高さ) の折れ線を Z 軸まわりに回した面（端は閉じる）"""
    rings = []
    for r, z in profile:
        if r < 1e-6:
            rings.append([bm.verts.new((0.0, 0.0, z * z_sign))])
            continue
        rings.append([bm.verts.new((r * math.cos(2 * math.pi * i / segments), r * math.sin(2 * math.pi * i / segments),
                                    z * z_sign)) for i in range(segments)])
    for a, b in zip(rings[:-1], rings[1:]):
        if len(a) == 1 and len(b) == 1:
            continue
        if len(a) == 1:
            for i in range(segments):
                bm.faces.new((a[0], b[i], b[(i + 1) % segments]))
        elif len(b) == 1:
            for i in range(segments):
                bm.faces.new((a[i], b[0], a[(i + 1) % segments]))
        else:
            for i in range(segments):
                bm.faces.new((a[i], b[i], b[(i + 1) % segments], a[(i + 1) % segments]))


def build_swing_metal(R, pointer_scale, patch_uv, patch_half):
    """球の中心が原点。帯は文字盤と同じ面（XZ）で、北極から見る人の左（-X）を回って南極へ（スケッチ）"""
    bm = bmesh.new()
    r_in = R * (1.0 + BAND_GAP)
    r_out = r_in + R * BAND_THICK
    hw = R * BAND_WIDTH * 0.5
    rings = []
    for i in range(BAND_SEGMENTS + 1):
        a = math.pi * i / BAND_SEGMENTS
        d = Vector((-math.sin(a), 0.0, math.cos(a)))
        rings.append([bm.verts.new(d * r + Vector((0.0, y, 0.0))) for r, y in
                      ((r_in, -hw), (r_out, -hw), (r_out, hw), (r_in, hw))])
    add_ring_quads(bm, rings)
    bm.faces.new(list(reversed(rings[0])))
    bm.faces.new(rings[-1])

    # 極の金具：ガラスに少し沈めた円錐台。帯の端を覆う
    cap_bottom = R * (1.0 - CAP_SINK)
    cap_top = r_out + R * CAP_LIP
    cap = [(0.0, cap_bottom), (R * CAP_R, cap_bottom), (R * CAP_TOP_R, cap_top)]
    # 北：上の面に針の根元がはまる短い筒（床は金具の上の面）
    s_in = POINTER_BASE_R * SOCKET_INNER * pointer_scale
    s_out = POINTER_BASE_R * SOCKET_OUTER * pointer_scale
    s_top = cap_top + SOCKET_DEPTH * pointer_scale
    add_lathe(bm, cap + [(s_out, cap_top), (s_out, s_top), (s_in, s_top), (s_in, cap_top), (0.0, cap_top)])
    # 南：同じ金具の先に小さな玉
    kr, kh = R * SOUTH_KNOB_R, R * SOUTH_KNOB_H
    add_lathe(bm, cap + [(kr, cap_top), (kr, cap_top + kh * 0.6), (kr * 0.5, cap_top + kh), (0.0, cap_top + kh)],
              z_sign=-1.0)
    # 面の向きは部品（つながった塊）ごとに外向きへそろえる（南は Z を反転したので逆を向いている）
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces[:])

    # UV：針のテクスチャの金の一点のまわりの小さな四角へ（三角形がつぶれない程度に位置で散らす）
    uv_layer = bm.loops.layers.uv.new("UVMap")
    span = 3.0 * R
    for f in bm.faces:
        for loop in f.loops:
            p = loop.vert.co
            a = ((p.x + p.y) / span) % 1.0
            b = ((p.z + p.y * 0.5) / span) % 1.0
            loop[uv_layer].uv = patch_uv + Vector(((a - 0.5) * 2.0 * patch_half, (b - 0.5) * 2.0 * patch_half))

    me = bpy.data.meshes.new("EtherVessel_SwingMetal")
    bm.to_mesh(me)
    bm.free()
    obj = bpy.data.objects.new(me.name, me)
    bpy.context.scene.collection.objects.link(obj)
    me.shade_smooth()
    me.set_sharp_from_angle(angle=math.radians(35.0))
    return obj, {"bandInner": r_in, "bandOuter": r_out, "bandHalfWidth": hw, "capTop": cap_top, "socketTop": s_top}


# ============================================================
# 組み立て
# ============================================================
def bake_scale_and_origin(obj, scale, origin_offset=Vector((0, 0, 0))):
    """拡大をメッシュへ焼き、原点を（拡大後の座標で）origin_offset だけずらす"""
    obj.data.transform(Matrix.Diagonal((scale, scale, scale, 1.0)))
    if origin_offset.length > 0:
        obj.data.transform(Matrix.Translation(-origin_offset))
    obj.scale = (1, 1, 1)
    obj.data.update()


def assemble():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    objs = {p: import_part(p) for p in PARTS}
    infos = {p: prepare_material(objs[p], p) for p in PARTS}
    ref = {p: rgb_to_hsv(image_pixels(infos[p]["base"])[..., :3]) for p in (ENAMEL_FROM, GOLD_FROM)}
    recolor_ring_stand(infos["RingStand"], ref)

    dial_scale = DIAL_OUTER_IN_RING / DIAL_OUTER_R * RING_SCALE
    R = SPHERE_OF_DIAL * DIAL_OUTER_R * dial_scale

    table_top = TABLE_TOP_Z * TABLE_SCALE
    pillar_z = table_top - PILLAR_FLANGE_Z * PILLAR_SCALE
    ring_base = pillar_z + PILLAR_SEAT_Z * PILLAR_SCALE
    center_z = ring_base + RING_PLANE_Z * RING_SCALE

    # 針：根元を差し込み口の床（北極の金具の上の面）に置き、先を数字の帯の内側の縁へ
    tip = (DIAL_BAND_INNER_R + POINTER_REACH) * dial_scale
    cap_top = R * (1.0 + BAND_GAP + BAND_THICK + CAP_LIP)
    pointer_scale = (tip - cap_top) / POINTER_H

    bake_scale_and_origin(objs["Table"], TABLE_SCALE)
    bake_scale_and_origin(objs["Pillar"], PILLAR_SCALE)
    bake_scale_and_origin(objs["RingStand"], RING_SCALE)
    bake_scale_and_origin(objs["Dial"], dial_scale, Vector((0, 0, DIAL_CENTER_Z * dial_scale)))
    bake_scale_and_origin(objs["Pointer"], pointer_scale, Vector((0, 0, -cap_top)))   # 原点＝球の中心
    objs["Pillar"].location = (0, 0, pillar_z)
    objs["RingStand"].location = (0, 0, ring_base)
    objs["Dial"].location = (0, DIAL_BACK, center_z)

    patch_uv, patch_half = find_gold_patch(infos[GOLD_FROM])
    metal, dims = build_swing_metal(R, pointer_scale, patch_uv, patch_half)
    metal.data.materials.append(infos["Pointer"]["material"])
    # Swing = 針 ＋ 帯と金具（材質は針と同じ 1 つ）。原点＝球の中心
    swing = objs.pop("Pointer")
    bpy.ops.object.select_all(action="DESELECT")
    metal.select_set(True)
    swing.select_set(True)
    bpy.context.view_layer.objects.active = swing
    bpy.ops.object.join()
    swing.name = swing.data.name = "EtherVessel_Swing"
    swing.location = (0, 0, center_z)
    objs["Swing"] = swing
    infos["Swing"] = infos.pop("Pointer")

    dims.update({"R": R, "centerZ": center_z, "tableTop": table_top, "pillarZ": pillar_z, "ringBase": ring_base,
                 "dialScale": dial_scale, "pointerScale": pointer_scale, "pointerTip": tip,
                 "pointerTop": cap_top + POINTER_H * pointer_scale})
    bpy.context.view_layer.update()
    top = max((o.matrix_world @ Vector(c)).z for o in objs.values() for c in o.bound_box)
    dims["totalHeight"] = top
    log(f"R={R:.4f} center z={center_z:.4f} dial scale={dial_scale:.4f} pointer scale={pointer_scale:.4f} "
        f"total height={top:.3f}")
    return objs, infos, dims


# ============================================================
# 書き出し
# ============================================================
def write_png(path, px):
    """8bit RGB の PNG を書く。glb に埋め込まれた画像は Image.save() が埋め込みの元のデータを書いてしまい、
    台座の色直し（ピクセルの書き換え）が消えるので、ピクセルから自分で書く。Blender のピクセルは下の行から並ぶ"""
    rgb = (np.clip(px[::-1, :, :3], 0.0, 1.0) * 255.0 + 0.5).astype(np.uint8)
    h, w, _ = rgb.shape
    raw = b"".join(bytes(1) + rgb[y].tobytes() for y in range(h))   # 各行の頭はフィルタ 0

    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    path.write_bytes(bytes.fromhex("89504e470d0a1a0a") + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
                     + chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))


def save_textures_as_png(infos, out_dir):
    """cook は PNG だけを DDS にする。glb の中の JPEG も PNG で書き出し、書いた PNG を読み直して材質へ差し替える
    （glTF の書き出しが埋め込みの元の画像を書かないように）"""
    for info in infos.values():
        for key in ("base", "normal"):
            img = info[key]
            name = img.name
            path = (out_dir / f"{name}.png").resolve()
            write_png(path, image_pixels(img))
            new = bpy.data.images.load(str(path))
            new.colorspace_settings.name = img.colorspace_settings.name
            for mat in bpy.data.materials:
                if mat.node_tree:
                    for n in mat.node_tree.nodes:
                        if n.type == "TEX_IMAGE" and n.image == img:
                            n.image = new
            bpy.data.images.remove(img)
            new.name = name
            info[key] = new
            log(f"texture {path}")


def flip_texcoord_v(gltf_path):
    """cook は glTF の V を必ず反転するが、Blender の glTF の UV はすでに左上が原点で正しいので、反転の逆を書いておく
    （rig_hub_mech.py と同じ）"""
    gltf = json.loads(gltf_path.read_text(encoding="utf-8"))
    accessor_ids = {prim["attributes"]["TEXCOORD_0"]
                    for mesh in gltf.get("meshes", []) for prim in mesh["primitives"]
                    if "TEXCOORD_0" in prim.get("attributes", {})}
    bin_path = gltf_path.parent / gltf["buffers"][0]["uri"]
    data = bytearray(bin_path.read_bytes())
    for ai in accessor_ids:
        acc = gltf["accessors"][ai]
        assert acc["componentType"] == 5126 and acc["type"] == "VEC2", acc
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


def export_object(obj, path):
    saved = obj.location.copy()
    obj.location = (0, 0, 0)
    bpy.ops.object.select_all(action="DESELECT")
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    bpy.ops.export_scene.gltf(filepath=str(path.resolve()), export_format="GLTF_SEPARATE", use_selection=True,
                              export_yup=True, export_apply=True, export_animations=False,
                              export_image_format="AUTO")
    obj.location = saved
    flip_texcoord_v(path)
    log(f"exported {path}")


def engine_pos(z, y=0.0):
    """Blender の (0, y, z) → エンジンの (0, z, -y)"""
    return [0.0, round(z, 4), round(-y, 4) + 0.0]


def write_geometry_json(objs, dims):
    data = {
        "_comment": "gen_ether_vessel.py が書き出す。手で直さず、スクリプトの定数を直して --export する（19_GameOverAndDifficulty.md §4.2）。"
                    "座標はエンジン（土台の脚の下が原点、正面 +Z、見る人の左＝文字盤の I の側が +X）",
        "parts": {name: {"model": f"{RESOURCE_DIR}/EtherVessel_{name}.mesh",
                         "position": engine_pos(objs[name].location.z, objs[name].location.y)}
                  for name in ("Table", "Pillar", "RingStand", "Dial", "Swing")},
        "sphere": {"center": engine_pos(dims["centerZ"]), "radius": round(dims["R"], 4)},
        "swing": {
            "_comment": "Swing（C の字の帯＋極の金具＋針）の原点＝球の中心。球と一緒にエンジンの Z 軸まわりに回す",
            "pivot": engine_pos(dims["centerZ"]), "axis": [0.0, 0.0, 1.0],
            "bandInner": round(dims["bandInner"], 4), "bandOuter": round(dims["bandOuter"], 4),
            "pointerTipDistance": round(dims["pointerTip"], 4),
        },
        "dial": {"center": engine_pos(dims["centerZ"], DIAL_BACK),
                 "outerRadius": round(DIAL_OUTER_R * dims["dialScale"], 4),
                 "numeralBandInnerRadius": round(DIAL_BAND_INNER_R * dims["dialScale"], 4),
                 "_comment": "目盛りは I〜IX の 9 個。I は真上から +X 側へ iDegrees、IX は -X 側へ iDegrees、V が真上",
                 "iDegrees": DIAL_I_DEG, "ticks": DIAL_TICKS,
                 "tickDegrees": 2.0 * DIAL_I_DEG / (DIAL_TICKS - 1)},
        "ringStand": {"ringY": round(dims["centerZ"], 4), "ringInnerRadius": round(RING_INNER_R * RING_SCALE, 4),
                      "ringOuterRadius": round(0.49 * RING_SCALE, 4)},
        "totalHeight": round(dims["totalHeight"], 4),
    }
    GEOMETRY_JSON.parent.mkdir(parents=True, exist_ok=True)
    GEOMETRY_JSON.write_text(json.dumps(data, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    log(f"wrote {GEOMETRY_JSON}")


def export_all(objs, infos, dims, out_dir):
    out_dir.mkdir(parents=True, exist_ok=True)
    save_textures_as_png(infos, out_dir)
    for name, obj in objs.items():
        export_object(obj, out_dir / f"EtherVessel_{name}.gltf")
    write_geometry_json(objs, dims)


# ============================================================
# プレビュー（球は仮の水色の半透明。ガラスと液体は G5 でエンジンが描く）
# ============================================================
def preview(objs, dims, out_dir):
    out_dir.mkdir(parents=True, exist_ok=True)
    sc = bpy.context.scene
    sc.render.engine = "BLENDER_EEVEE_NEXT"
    sc.render.resolution_x = sc.render.resolution_y = PREVIEW_RES
    world = bpy.data.worlds.new("Preview")
    sc.world = world
    world.use_nodes = True
    world.node_tree.nodes["Background"].inputs[0].default_value = (0.5, 0.5, 0.55, 1.0)
    sun = bpy.data.objects.new("Sun", bpy.data.lights.new("Sun", "SUN"))
    sun.data.energy = 3.0
    sun.rotation_euler = (0.7, 0.2, -0.5)
    sc.collection.objects.link(sun)

    glass = bpy.data.materials.new("PreviewGlass")
    glass.use_nodes = True
    bsdf = glass.node_tree.nodes["Principled BSDF"]
    bsdf.inputs["Base Color"].default_value = (0.55, 0.85, 1.0, 1.0)
    bsdf.inputs["Alpha"].default_value = 0.35
    bm = bmesh.new()
    bmesh.ops.create_uvsphere(bm, u_segments=48, v_segments=24, radius=dims["R"])
    me = bpy.data.meshes.new("PreviewSphere")
    bm.to_mesh(me)
    bm.free()
    me.materials.append(glass)
    sphere = bpy.data.objects.new("PreviewSphere", me)
    sc.collection.objects.link(sphere)
    sphere.parent = objs["Swing"]

    cam = bpy.data.objects.new("Cam", bpy.data.cameras.new("Cam"))
    sc.collection.objects.link(cam)
    sc.camera = cam
    h = dims["totalHeight"]

    def shot(name, d, target, ortho=None, lens=50.0):
        cam.data.type = "ORTHO" if ortho else "PERSP"
        if ortho:
            cam.data.ortho_scale = ortho
        cam.data.lens = lens
        cam.location = target + d
        cam.rotation_euler = (-d).to_track_quat("-Z", "Y").to_euler()
        sc.render.filepath = str((out_dir / f"{name}.png").resolve())
        bpy.ops.render.render(write_still=True)

    mid = Vector((0, 0, h * 0.5))
    c = Vector((0, 0, dims["centerZ"]))
    swing = objs["Swing"]
    swing.rotation_mode = "XYZ"     # glTF の読み込みはクォータニオンにするので、オイラーで回す
    shot("front", Vector((0, -20, 0)), mid, ortho=h * 1.1)
    shot("side", Vector((20, 0, 0)), mid, ortho=h * 1.1)
    e = math.radians(35.0)
    shot("look35", Vector((0, -math.cos(e), math.sin(e))) * h * 2.2, Vector((0, 0, h * 0.6)))
    shot("dial_front", Vector((0, -20, 0)), c, ortho=dims["dialScale"] * 1.05)
    for label, deg in (("tilt_I", -50.0), ("tilt_IX", 50.0)):
        swing.rotation_euler = (0.0, math.radians(deg), 0.0)
        bpy.context.view_layer.update()
        shot(label, Vector((0, -20, 0)), c, ortho=dims["dialScale"] * 1.05)
    swing.rotation_euler = (0.0, 0.0, 0.0)
    pole = c + Vector((0, 0, dims["capTop"]))
    shot("pole", Vector((-0.6, -1.0, 0.45)).normalized() * dims["R"] * 2.5, pole, lens=50.0)


def main():
    opts = parse_args()
    objs, infos, dims = assemble()
    if "export" in opts:
        export_all(objs, infos, dims, Path(opts["export"]))
    if "save" in opts:
        bpy.ops.wm.save_as_mainfile(filepath=str(Path(opts["save"]).resolve()))
    if "preview" in opts:
        preview(objs, dims, Path(opts["preview"]))


if __name__ == "__main__":
    main()
