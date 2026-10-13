"""HUB 用の機体（Tripo で生成、部位分割済みの glb）を、動かす単位にまとめてボーンを付けるスクリプト。

使い方（Project/ で実行）:
    blender -b --factory-startup -P tools/BlenderPipeline/rig_hub_mech.py -- [オプション]
        --src <glb>            元のモデル（既定 Assets/Models/HubMech/HubMech.glb）
        --preview <フォルダ>   グループの色分け（正面・横・背面）と、HUB のポーズ（正面・横・斜め）の PNG
        --save <パス.blend>    結果を .blend で保存
        --export <フォルダ>    HubMech / HubMech_Cockpit / HubMech_Hatch の glTF とテクスチャ(PNG)を書き出す
                               （例: Assets/Models/HubMech/Export → cook_assets.py で Resources へ）

HUB の機体は二本足で立ったまま、右手（モデルの右＝ -X 側）の手のひらを胸の開口部の下の縁へ差し出し、
パイロットが手のひらを伝ってコクピットへ入れる形（17_HubScene.md §5 ①、18_HubTransitions.md ①）。
Tripo の部位（tripo_part_N）をボーンごとのグループにまとめ、グループごとに 1 本のボーンへウェイト 1.0 を付ける（リジッドスキニング）。
Tripo で 1 つにつながっている部位（右腕の上腕＋前腕＝39、左脚の太もも＋すね＝8）は関節の高さで 2 つに分ける（切り口は少し重ねて穴を見せない）。

座標（Blender）: X=右 / Y=奥（正面は -Y 側から見る）/ Z=上。元のモデルは足元 Z=0・全高 ≈0.98（書き出し時に 18m へ拡大する）。
関節の位置・ポーズはすべて元のモデルの座標で書く。
書き出しはポーズを焼き込んだ静的なメッシュ（翼と翼の後ろの板を除いた体の高さ＝18m）。
AI が作ったコクピットの中身は取り除き、スクリプトで作ったコクピット（HubMech_Cockpit、本体と同じ座標）と
ハッチ（HubMech_Hatch、原点＝回転の軸。X 軸まわりに回して開け閉めする）に置き換える。
"""

import json
import math
import struct
import sys
from pathlib import Path

import bmesh
import bpy
from mathutils import Matrix, Quaternion, Vector

# ============================================================
# ボーンとグループ（Tripo の部位番号）。表に無い部位はすべて Pelvis（胴・頭・翼・左腕・腰の装甲）
# ============================================================
GROUPS = {
    "ShoulderArmor_R": [106, 121, 64, 114, 119, 49],
    "UpperArm_R": [55, 80],
    "ForeArm_R": [98, 113, 71],
    "Hand_R": [92, 90, 125],
    "Thigh_R": [2, 56, 67],
    "Shin_R": [3, 6, 75, 99, 41, 42],
    "Foot_R": [23, 91, 22],
    "Thigh_L": [31, 59],
    "Shin_L": [15, 72, 112, 120, 122, 111, 37, 44],
    "Foot_L": [110, 73, 60, 123, 18],
    "Binder_R": [1],
    "Binder_L": [0],
    "Wing_R": [5, 13, 27, 33, 50, 52, 63, 65, 66, 68, 78, 84, 87, 102, 103, 104, 108, 124],
    "Wing_L": [11, 53, 43, 20, 12, 17, 79, 115, 77, 9, 4, 109, 19, 35, 30, 54, 28, 105, 96, 83],
}
# AI が作ったコクピットの中身（内壁・座席）。取り除いて、スクリプトで作ったコクピットに置き換える。
# 開口部の枠（32）は外側なので残す
COCKPIT_REMOVE = [21, 40, 47, 62, 93, 74, 82]   # 74・82 は開口部の左右のとがった飾り（内枠と合わないので消す）

# 1 つにつながっている部位を高さで上下に分ける：(部位, 高さ, 上のボーン, 下のボーン)
SPLITS = [
    (39, 0.595, "UpperArm_R", "ForeArm_R"),
    (8, 0.355, "Thigh_L", "Shin_L"),
]
SPLIT_OVERLAP = 0.006        # 切り口を両側へ重ねる幅

# 関節（元のモデルの座標）
HIP_CENTER = Vector((0.0, -0.030, 0.530))
JOINTS = {
    "Shoulder_R": Vector((-0.115, -0.020, 0.675)),
    "Elbow_R": Vector((-0.128, -0.020, 0.595)),
    "Wrist_R": Vector((-0.172, -0.040, 0.505)),
    "HandTip_R": Vector((-0.165, -0.055, 0.420)),
    "Hip_R": Vector((-0.068, -0.030, 0.530)),
    "Knee_R": Vector((-0.092, -0.040, 0.355)),
    "Ankle_R": Vector((-0.137, -0.030, 0.100)),
    "Toe_R": Vector((-0.156, -0.140, 0.025)),
    "Hip_L": Vector((0.068, -0.030, 0.530)),
    "Knee_L": Vector((0.092, -0.040, 0.355)),
    "Ankle_L": Vector((0.137, -0.030, 0.100)),
    "Toe_L": Vector((0.156, -0.140, 0.025)),
}

# ボーン：名前 → (head, tail, 親)。Pelvis 以下が体、Root は地面
BONE_DEFS = [
    ("Root", Vector((0, 0, 0)), Vector((0, 0, 0.1)), None),
    ("Pelvis", HIP_CENTER, HIP_CENTER + Vector((0, 0, 0.15)), "Root"),
    ("ShoulderArmor_R", JOINTS["Shoulder_R"], JOINTS["Shoulder_R"] + Vector((-0.06, 0, 0)), "Pelvis"),
    ("UpperArm_R", JOINTS["Shoulder_R"], JOINTS["Elbow_R"], "Pelvis"),
    ("ForeArm_R", JOINTS["Elbow_R"], JOINTS["Wrist_R"], "UpperArm_R"),
    ("Hand_R", JOINTS["Wrist_R"], JOINTS["HandTip_R"], "ForeArm_R"),
    ("Thigh_R", JOINTS["Hip_R"], JOINTS["Knee_R"], "Pelvis"),
    ("Shin_R", JOINTS["Knee_R"], JOINTS["Ankle_R"], "Thigh_R"),
    ("Foot_R", JOINTS["Ankle_R"], JOINTS["Toe_R"], "Shin_R"),
    ("Thigh_L", JOINTS["Hip_L"], JOINTS["Knee_L"], "Pelvis"),
    ("Shin_L", JOINTS["Knee_L"], JOINTS["Ankle_L"], "Thigh_L"),
    ("Foot_L", JOINTS["Ankle_L"], JOINTS["Toe_L"], "Shin_L"),
    ("Binder_R", Vector((-0.120, 0.100, 0.850)), Vector((-0.200, 0.100, 0.150)), "Pelvis"),
    ("Binder_L", Vector((0.120, 0.100, 0.850)), Vector((0.200, 0.100, 0.150)), "Pelvis"),
    ("Wing_R", Vector((-0.090, 0.060, 0.820)), Vector((-0.300, 0.060, 0.900)), "Pelvis"),
    ("Wing_L", Vector((0.090, 0.060, 0.820)), Vector((0.300, 0.060, 0.900)), "Pelvis"),
]
BONE_NAMES = [b[0] for b in BONE_DEFS]

# ============================================================
# HUB のポーズ（二本足で立ったまま、右手を開口部の下の縁へ）
# ============================================================
SHOULDER_SHIFT = Vector((0.010, -0.025, -0.005))  # 右肩をわずかに前へ（腕が短いので開口部の下まで届かせる）
SHOULDER_ARMOR_FOLLOW = 0.35             # 肩アーマーは腕の回転のこの割合だけ回す（胴や翼へのめり込みを避ける）
REST_FOREARM_BACK = Vector((0.0, 1.0, 0.0))  # 前腕の刃（71）が付いている側。ポーズでは下へ向ける
PALM_TARGET = Vector((0.000, -0.150, 0.535))   # 手のひらの中心（開口部の下の縁 z≈0.548・前面 y≈-0.122 のすぐ手前・下）
HAND_DIR = Vector((1.0, -0.3, 0.0))      # 指先の向き。腕が短く前腕は縁の下を通れないので、前腕は開口部の横に置き、手を縁の下へ横向きに渡す
PALM_FROM_WRIST = 0.050                  # 手首から手のひらの中心まで
REST_PALM_NORMAL = Vector((1.0, 0.0, 0.0))  # 元のポーズで手のひらが向いている向き（体の内側）

GROUP_COLORS = {
    "Pelvis": (0.55, 0.55, 0.58),
    "ShoulderArmor_R": (0.95, 0.75, 0.15),
    "UpperArm_R": (0.90, 0.25, 0.25),
    "ForeArm_R": (0.25, 0.55, 0.95),
    "Hand_R": (0.30, 0.85, 0.40),
    "Thigh_R": (0.85, 0.35, 0.80),
    "Shin_R": (0.20, 0.80, 0.80),
    "Foot_R": (0.95, 0.50, 0.20),
    "Thigh_L": (0.85, 0.35, 0.80),
    "Shin_L": (0.20, 0.80, 0.80),
    "Foot_L": (0.95, 0.50, 0.20),
    "Binder_R": (0.35, 0.30, 0.75),
    "Binder_L": (0.35, 0.30, 0.75),
    "Wing_R": (0.95, 0.95, 0.55),
    "Wing_L": (0.95, 0.95, 0.55),
}


# ============================================================
# 読み込みとグループ分け
# ============================================================
def part_index(obj):
    return int(obj.name.split("_")[-1])


def import_source(path):
    bpy.ops.import_scene.gltf(filepath=str(path))
    meshes = [o for o in bpy.context.scene.objects if o.type == "MESH"]
    # 親の変換を焼いて、以降はメッシュの座標＝元のモデルの座標として扱う
    for o in meshes:
        mw = o.matrix_world.copy()
        o.parent = None
        o.data.transform(mw)
        o.matrix_world = Matrix.Identity(4)
    for o in [o for o in bpy.context.scene.objects if o.type != "MESH"]:
        bpy.data.objects.remove(o)
    return meshes


def split_by_height(obj, z, overlap):
    """obj を高さ z で上下 2 つに分ける。各面は重なり分だけ両側に残す"""
    lower = obj.copy()
    lower.data = obj.data.copy()
    bpy.context.scene.collection.objects.link(lower)
    for target, keep_above in ((obj, True), (lower, False)):
        bm = bmesh.new()
        bm.from_mesh(target.data)
        if keep_above:
            drop = [f for f in bm.faces if all(v.co.z < z - overlap for v in f.verts)]
        else:
            drop = [f for f in bm.faces if all(v.co.z > z + overlap for v in f.verts)]
        bmesh.ops.delete(bm, geom=drop, context="FACES")
        bm.to_mesh(target.data)
        bm.free()
    return obj, lower


def group_parts(meshes):
    groups = {name: [] for name in GROUP_COLORS}
    table = {idx: bone for bone, ids in GROUPS.items() for idx in ids}
    splits = {s[0]: s for s in SPLITS}
    for o in meshes:
        idx = part_index(o)
        if idx in COCKPIT_REMOVE:
            bpy.data.objects.remove(o)
            continue
        if idx in splits:
            _, z, upper_bone, lower_bone = splits[idx]
            upper, lower = split_by_height(o, z, SPLIT_OVERLAP)
            groups[upper_bone].append(upper)
            groups[lower_bone].append(lower)
            continue
        groups[table.get(idx, "Pelvis")].append(o)
    return groups


def report(groups):
    total = 0
    for name, objs in groups.items():
        tris = sum(sum(len(p.vertices) - 2 for p in o.data.polygons) for o in objs)
        total += tris
        print(f"[rig_hub_mech] {name}: {len(objs)} parts, {tris} tris")
    print(f"[rig_hub_mech] total {total} tris")


# ============================================================
# ボーンとウェイト
# ============================================================
def build_armature():
    arm_data = bpy.data.armatures.new("HubMechRig")
    arm = bpy.data.objects.new("HubMechRig", arm_data)
    bpy.context.scene.collection.objects.link(arm)
    bpy.context.view_layer.objects.active = arm
    bpy.ops.object.mode_set(mode="EDIT")
    eb = arm_data.edit_bones
    for name, head, tail, parent in BONE_DEFS:
        b = eb.new(name)
        b.head, b.tail = head, tail
        if parent is not None:
            b.parent = eb[parent]
    bpy.ops.object.mode_set(mode="OBJECT")
    return arm


def join_with_weights(groups, arm):
    """グループごとに頂点グループ（ボーン名・ウェイト 1.0）を付け、全部を 1 つのメッシュにまとめる"""
    objs = []
    for bone, parts in groups.items():
        for o in parts:
            vg = o.vertex_groups.new(name=bone)
            vg.add(range(len(o.data.vertices)), 1.0, "REPLACE")
            # プレビューでグループを色分けするための頂点カラー（書き出しには使わない）
            attr = o.data.color_attributes.new("GroupColor", "FLOAT_COLOR", "POINT")
            for d in attr.data:
                d.color = (*GROUP_COLORS[bone], 1.0)
            objs.append(o)
    bpy.ops.object.select_all(action="DESELECT")
    for o in objs:
        o.select_set(True)
    bpy.context.view_layer.objects.active = objs[0]
    bpy.ops.object.join()
    mesh = bpy.context.view_layer.objects.active
    mesh.name = "HubMech"
    mod = mesh.modifiers.new("Armature", "ARMATURE")
    mod.object = arm
    mesh.parent = arm
    return mesh


# ============================================================
# HUB のポーズ
# ============================================================
def two_bone_ik(root, mid, end, target, pole):
    """root→mid→end の 2 本の骨を、end が target に来るように曲げる。曲がる向きは pole 側。
    戻り値：(mid の新しい位置, end の新しい位置)"""
    l1 = (mid - root).length
    l2 = (end - mid).length
    to_t = target - root
    dist = min(max(to_t.length, 1e-5), l1 + l2 - 1e-5)
    axis = to_t.normalized()
    cos_a = (l1 * l1 + dist * dist - l2 * l2) / (2.0 * l1 * dist)
    a = math.acos(max(-1.0, min(1.0, cos_a)))
    side = pole - axis * pole.dot(axis)
    side = side.normalized() if side.length > 1e-6 else Vector((0, -1, 0))
    new_mid = root + axis * (math.cos(a) * l1) + side * (math.sin(a) * l1)
    new_end = root + axis * dist
    return new_mid, new_end


def aim(rest_from, rest_to, new_from, new_to):
    return (rest_to - rest_from).normalized().rotation_difference((new_to - new_from).normalized())


def pose_hub(arm):
    """二本足で立ったまま、右手の手のひらを開口部の下の縁へ差し出す。
    各ボーンの回転（ワールド）と根元の位置を求め、アーマチュア空間の行列で与える"""
    J = JOINTS
    poses = {}   # bone -> (回転, 根元の位置)

    # --- 右腕：手首を開口部の下の縁へ ---
    shoulder = J["Shoulder_R"] + SHOULDER_SHIFT
    hand_dir = HAND_DIR.normalized()
    wrist_t = PALM_TARGET - hand_dir * PALM_FROM_WRIST
    elbow_rest = J["Elbow_R"] + SHOULDER_SHIFT
    wrist_rest = J["Wrist_R"] + SHOULDER_SHIFT
    elbow, wrist = two_bone_ik(shoulder, elbow_rest, wrist_rest, wrist_t, Vector((-1.0, 0.2, -0.5)))
    q_upper = aim(J["Shoulder_R"], J["Elbow_R"], shoulder, elbow)
    q_fore = aim(J["Elbow_R"], J["Wrist_R"], elbow, wrist)
    # 前腕をひねって、刃の付いた側を下へ向ける（開口部の前をふさがないように）
    fore_dir = (wrist - elbow).normalized()
    b = q_fore @ REST_FOREARM_BACK
    b_p = (b - fore_dir * b.dot(fore_dir)).normalized()
    down = Vector((0, 0, -1))
    d_p = (down - fore_dir * down.dot(fore_dir)).normalized()
    q_fore = b_p.rotation_difference(d_p) @ q_fore
    # 手：指先を hand_dir へ向け、手のひらを上へひねる
    q_hand = aim(J["Wrist_R"], J["HandTip_R"], wrist, wrist + hand_dir)
    n = q_hand @ REST_PALM_NORMAL
    up = Vector((0, 0, 1))
    n_p = (n - hand_dir * n.dot(hand_dir)).normalized()
    up_p = (up - hand_dir * up.dot(hand_dir)).normalized()
    q_hand = n_p.rotation_difference(up_p) @ q_hand
    q_armor = Quaternion().slerp(q_upper, SHOULDER_ARMOR_FOLLOW)
    poses["ShoulderArmor_R"] = (q_armor, shoulder)
    poses["UpperArm_R"] = (q_upper, shoulder)
    poses["ForeArm_R"] = (q_fore, elbow)
    poses["Hand_R"] = (q_hand, wrist)
    print(f"[rig_hub_mech] wrist reach error {(wrist - wrist_t).length:.4f}")

    bpy.context.view_layer.objects.active = arm
    bpy.ops.object.mode_set(mode="POSE")
    for name in BONE_NAMES:   # 親から順に（BONE_DEFS は親が先）
        if name not in poses:
            continue
        q, head = poses[name]
        rest = arm.data.bones[name].matrix_local.to_3x3()
        m = (q.to_matrix() @ rest).to_4x4()
        m.translation = head
        arm.pose.bones[name].matrix = m
        bpy.context.view_layer.update()
    bpy.ops.object.mode_set(mode="OBJECT")


# ============================================================
# 大きさ（翼と翼の後ろの板を除いた体の高さを 18m に）
# ============================================================
BODY_HEIGHT_M = 18.0
NON_BODY_GROUPS = ("Wing_R", "Wing_L", "Binder_R", "Binder_L")


def body_top_z(groups):
    return max(v.co.z for name, objs in groups.items() if name not in NON_BODY_GROUPS
               for o in objs for v in o.data.vertices)


def bake_pose(mesh, arm):
    """ポーズをメッシュへ焼き込み、アーマチュアとプレビュー用の情報を外して静的なメッシュにする"""
    bpy.ops.object.select_all(action="DESELECT")
    bpy.context.view_layer.objects.active = mesh
    mesh.select_set(True)
    bpy.ops.object.modifier_apply(modifier="Armature")
    mesh.parent = None
    mesh.vertex_groups.clear()
    if "GroupColor" in mesh.data.color_attributes:
        mesh.data.color_attributes.remove(mesh.data.color_attributes["GroupColor"])
    bpy.data.objects.remove(arm)


# ============================================================
# コクピットとハッチ（ガンダムのコクピットを参考に、左右対称で手続き生成。単位 m）
# コクピットのローカル座標：原点＝横に倒した円柱の軸の中央、X=右 / -Y=前 / Z=上。
# 円柱の上の点は (x, -r·cosφ, r·sinφ)。φ は前方向から上へ測った角度（度）。
# ============================================================
COCKPIT_CENTER = Vector((0.0, -0.059, 0.625))   # 円柱の軸の中央（元のモデルの座標。開口部の中央・前面から半径ぶん奥）
CP_R = 1.10                  # 円柱の半径（直径 2.2m）
CP_HALF_LEN = 1.00           # 円柱の半分の長さ（幅 2.0m）
SHELL_T = 0.04               # 外殻の厚み
APERTURE_HALF_W = 0.70       # 出入り口の半幅（幅 1.4m）
APERTURE_HALF_H = 0.90       # 出入り口の半分の高さ（高さ 1.8m）
APERTURE_BUMP_HALF_W = 0.22  # 出入り口の上下の縁の中央の台形（スケッチ）の上辺の半幅
APERTURE_BUMP_RAMP = 0.10    # 台形の斜めの部分の幅
APERTURE_BUMP_H = 0.12       # 台形の高さ（下の縁は上へ、上の縁は下へ出る）
APERTURE_PHI = 56.0          # 外殻を切り抜く角度（±）。出入り口の高さより少し大きく
# 内枠（出入り口を囲む固定の板）。AI の開口部の枠（幅 約2.2m × 高さ 約3.1m、横に倒した円柱のように前へ膨らむ）の
# すぐ裏に沿わせるため、半径の大きい円柱（軸は後ろ）の一部にする。ハッチは内枠の裏を回るので当たらない
BEZEL_AXIS_Y = 1.18          # 内枠の円柱の軸（コクピットの軸より後ろ）
BEZEL_R = 2.44               # 内枠の前面の半径（中央で y=-1.26、AI の枠の前面 -1.32 の少し裏）
BEZEL_T = 0.05
BEZEL_HALF_W = 1.05          # AI の枠の内側（|x|≈1.0〜1.14）の裏まで
BEZEL_HALF_H = 1.72          # AI の枠の上の縁（z≈1.6〜1.65）の裏まで。足りないと後ろが透ける
BEZEL_HOLE_BOTTOM = -1.05    # 内枠の穴の下の縁。下の固定の板（台形の出っ張り）が見えるように出入り口より下げる
LOWER_PLATE_BOTTOM = -1.10   # 下の固定の板（ハッチと同じ半径）の下の端。内枠の裏に隠れる
ENCLOSURE_BACK_Y = 0.60      # 内枠の裏を囲う箱の奥（AI のコクピットを消した穴から後ろが透けないように）
SHARP_ANGLE_DEG = 35.0       # これより折れている辺だけ角を立てる（曲面はなめらかに）
SEAL_W = 0.08                # 出入り口の左右の内側の目隠し（ハッチの横のすき間から光が入らないように）
HATCH_R_IN = 1.14            # ハッチ（円柱の一部の曲面の板）の内側・外側の半径。外殻の外を回る
HATCH_R_OUT = 1.20
HATCH_HALF_W = 0.68         # 出入り口（0.70）より少し狭く、出入り口の左右の壁の間を回る
HATCH_OPEN_DEG = 115.0       # 開いた状態（X 軸まわりに -この角度。上・奥の外殻の外側へしまう）
EYE = Vector((0.0, 0.18, 0.20))   # 座ったパイロットの目（プレビュー用）

CP_COLORS = {   # 名前: (sRGB, metallic, roughness)
    "CP_Frame":  ((0.13, 0.14, 0.16), 0.6, 0.45),
    "CP_Screen": ((0.03, 0.05, 0.06), 0.0, 0.15),
    "CP_Seat":   ((0.18, 0.18, 0.20), 0.1, 0.70),
    "CP_Metal":  ((0.38, 0.40, 0.43), 0.8, 0.35),
    "CP_Glow":   ((0.30, 1.00, 0.50), 0.0, 0.50),
    "CP_Light":  ((1.00, 0.98, 0.92), 0.0, 0.50),
    "HT_Navy":   ((0.20, 0.25, 0.34), 0.3, 0.40),
    "HT_Gold":   ((0.74, 0.62, 0.36), 1.0, 0.35),
    "HT_Inner":  ((0.10, 0.10, 0.11), 0.6, 0.50),
    "BZ_Navy":   ((0.16, 0.20, 0.28), 0.3, 0.45),
}


def srgb_to_linear(c):
    return tuple(((x / 12.92) if x <= 0.04045 else ((x + 0.055) / 1.055) ** 2.4) for x in c)


def cp_materials():
    mats = {}
    for name, (c, metal, rough) in CP_COLORS.items():
        m = bpy.data.materials.new(name)
        lin = srgb_to_linear(c)
        m.diffuse_color = (*lin, 1.0)
        m.use_nodes = True
        b = m.node_tree.nodes["Principled BSDF"]
        b.inputs["Base Color"].default_value = (*lin, 1.0)
        b.inputs["Metallic"].default_value = metal
        b.inputs["Roughness"].default_value = rough
        mats[name] = m
    return mats


def cyl(r, phi_deg, x):
    p = math.radians(phi_deg)
    return Vector((x, -r * math.cos(p), r * math.sin(p)))


class MeshBuilder:
    """面の一覧を貯めて 1 つのオブジェクトにする。立体は閉じた形で足す"""

    def __init__(self):
        self.verts = []
        self.faces = []
        self.mats = []
        self.mat_names = []

    def _mi(self, mat):
        if mat not in self.mat_names:
            self.mat_names.append(mat)
        return self.mat_names.index(mat)

    def add(self, verts, faces, mat):
        base = len(self.verts)
        self.verts += [Vector(v) for v in verts]
        mi = self._mi(mat)
        for f in faces:
            self.faces.append([base + i for i in f])
            self.mats.append(mi)

    def box(self, center, size, mat, rot=None):
        c = Vector(center)
        hx, hy, hz = (s * 0.5 for s in size)
        R = rot.to_matrix() if rot is not None else Matrix.Identity(3)
        corners = [Vector((sx * hx, sy * hy, sz * hz)) for sz in (-1, 1) for sy in (-1, 1) for sx in (-1, 1)]
        self.add([c + R @ v for v in corners],
                 [(0, 2, 3, 1), (4, 5, 7, 6), (0, 1, 5, 4), (2, 6, 7, 3), (0, 4, 6, 2), (1, 3, 7, 5)], mat)

    def slab_grid(self, inner, outer, mat):
        """inner / outer は同じ大きさの格子（[行][列] の点）。2 枚の面と 4 辺の側面で閉じた板にする"""
        rows, cols = len(inner), len(inner[0])
        verts = [p for grid in (inner, outer) for row in grid for p in row]
        n = rows * cols

        def idx(g, r, c):
            return g * n + r * cols + c

        faces = []
        for r in range(rows - 1):
            for c in range(cols - 1):
                faces.append((idx(0, r, c), idx(0, r + 1, c), idx(0, r + 1, c + 1), idx(0, r, c + 1)))
                faces.append((idx(1, r, c), idx(1, r, c + 1), idx(1, r + 1, c + 1), idx(1, r + 1, c)))
        for r in range(rows - 1):
            for c in (0, cols - 1):
                faces.append((idx(0, r, c), idx(1, r, c), idx(1, r + 1, c), idx(0, r + 1, c)))
        for c in range(cols - 1):
            for r in (0, rows - 1):
                faces.append((idx(0, r, c), idx(0, r, c + 1), idx(1, r, c + 1), idx(1, r, c)))
        self.add(verts, faces, mat)

    def cyl_panel(self, r0, r1, phi0, phi1, x0, x1, mat, seg=None):
        seg = seg or max(1, int(abs(phi1 - phi0) / 6.0))
        inner = [[cyl(r0, phi0 + (phi1 - phi0) * k / seg, x) for x in (x0, x1)] for k in range(seg + 1)]
        outer = [[cyl(r1, phi0 + (phi1 - phi0) * k / seg, x) for x in (x0, x1)] for k in range(seg + 1)]
        self.slab_grid(inner, outer, mat)

    def disc_sector(self, x0, x1, r0, r1, a0, a1, mat, seg=None):
        """X 軸まわりの円盤の扇形（端の壁・輪）。a は φ と同じ測り方（度）"""
        seg = seg or max(1, int(abs(a1 - a0) / 6.0))
        inner = [[cyl(r, a0 + (a1 - a0) * k / seg, x0) for r in (r0, r1)] for k in range(seg + 1)]
        outer = [[cyl(r, a0 + (a1 - a0) * k / seg, x1) for r in (r0, r1)] for k in range(seg + 1)]
        self.slab_grid(inner, outer, mat)

    def rod(self, p0, p1, radius, mat, sides=10):
        p0, p1 = Vector(p0), Vector(p1)
        d = (p1 - p0).normalized()
        u = d.orthogonal().normalized()
        v = d.cross(u)

        def ring(p):
            return [p + (u * math.cos(2 * math.pi * k / sides) + v * math.sin(2 * math.pi * k / sides)) * radius
                    for k in range(sides)]

        verts = ring(p0) + ring(p1)
        faces = [(k, (k + 1) % sides, sides + (k + 1) % sides, sides + k) for k in range(sides)]
        faces.append(tuple(reversed(range(sides))))
        faces.append(tuple(range(sides, 2 * sides)))
        self.add(verts, faces, mat)

    def build(self, name, mats):
        me = bpy.data.meshes.new(name)
        me.from_pydata([tuple(v) for v in self.verts], [], self.faces)
        for m in self.mat_names:
            me.materials.append(mats[m])
        for poly, mi in zip(me.polygons, self.mats):
            poly.material_index = mi
        me.update()
        bm = bmesh.new()
        bm.from_mesh(me)
        bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
        bm.to_mesh(me)
        bm.free()
        me.shade_smooth()
        me.set_sharp_from_angle(angle=math.radians(SHARP_ANGLE_DEG))
        obj = bpy.data.objects.new(name, me)
        bpy.context.scene.collection.objects.link(obj)
        return obj


def aperture_edge_z(x, top):
    """出入り口の上下の縁の高さ。中央に台形（下の縁は上へ、上の縁は下へ出っ張る）"""
    ax = abs(x)
    if ax <= APERTURE_BUMP_HALF_W:
        t = 1.0
    else:
        t = max(0.0, 1.0 - (ax - APERTURE_BUMP_HALF_W) / APERTURE_BUMP_RAMP)
    if top:
        return APERTURE_HALF_H - APERTURE_BUMP_H * t
    return -APERTURE_HALF_H + APERTURE_BUMP_H * t


def aperture_outline(n_per_side=24):
    """出入り口の輪郭（前から見た x, z）。下の縁を左→右、上の縁を右→左"""
    xs = [-APERTURE_HALF_W + 2 * APERTURE_HALF_W * k / n_per_side for k in range(n_per_side + 1)]
    pts = [(x, aperture_edge_z(x, top=False)) for x in xs]
    pts += [(x, aperture_edge_z(x, top=True)) for x in reversed(xs)]
    return pts


def build_shell(mb):
    r0, r1 = CP_R - SHELL_T * 0.5, CP_R + SHELL_T * 0.5
    bands = [(-CP_HALF_LEN, -APERTURE_HALF_W), (-APERTURE_HALF_W, -0.25), (-0.25, 0.25),
             (0.25, APERTURE_HALF_W), (APERTURE_HALF_W, CP_HALF_LEN)]
    step = 25.0
    for x0, x1 in bands:
        side = min(abs(x0), abs(x1)) >= APERTURE_HALF_W - 1e-6
        start, end = (-180.0, 180.0) if side else (APERTURE_PHI, 360.0 - APERTURE_PHI)
        n = max(1, round((end - start) / step))
        for k in range(n):
            a0 = start + (end - start) * k / n
            a1 = start + (end - start) * (k + 1) / n
            mb.cyl_panel(r0, r1, a0, a1, x0, x1, "CP_Frame")
            # 内側のモニターの板（枠より少し小さく、内側へ少し出す）
            m = 0.035
            da = math.degrees(m / CP_R)
            mb.cyl_panel(r0 - 0.012, r0, a0 + da, a1 - da, x0 + m, x1 - m, "CP_Screen")
    # 左右の端の壁（円盤）と、内側の切り欠きのある輪（参考：コクピットの側面）
    for sx in (-1.0, 1.0):
        xo, xi = sx * (CP_HALF_LEN + 0.04), sx * CP_HALF_LEN
        mb.disc_sector(min(xo, xi), max(xo, xi), 0.0, r1, -180.0, 180.0, "CP_Frame", seg=48)
        xr = xi - sx * 0.035
        for k in range(6):
            a0 = -180.0 + 60.0 * k + 6.0
            mb.disc_sector(min(xr, xi), max(xr, xi), 0.48, 0.70, a0, a0 + 48.0, "CP_Metal")


def bezel_y(z, offset=0.0):
    """内枠の前面（offset>0 で裏へ）の y"""
    return BEZEL_AXIS_Y - math.sqrt(max(BEZEL_R ** 2 - z * z, 0.0)) + offset


def build_bezel(mb):
    """出入り口を囲む固定の内枠（AI の枠の裏に沿う曲面の板）と、出入り口の左右の壁・目隠し"""
    nz = 40

    def strip(x0, x1, z_lo, z_hi, nx=8):
        """x0..x1 の帯。z の範囲は x ごとに z_lo(x)..z_hi(x)"""
        xs = [x0 + (x1 - x0) * k / nx for k in range(nx + 1)]
        front, back = [], []
        for j in range(nz + 1):
            fr, bk = [], []
            for x in xs:
                z = z_lo(x) + (z_hi(x) - z_lo(x)) * j / nz
                fr.append(Vector((x, bezel_y(z), z)))
                bk.append(Vector((x, bezel_y(z, BEZEL_T), z)))
            front.append(fr)
            back.append(bk)
        mb.slab_grid(front, back, "BZ_Navy")

    W, H, A = BEZEL_HALF_W, BEZEL_HALF_H, APERTURE_HALF_W
    strip(-W, -A, lambda x: -H, lambda x: H)                                   # 左
    strip(A, W, lambda x: -H, lambda x: H)                                     # 右
    strip(-A, A, lambda x: aperture_edge_z(x, top=True), lambda x: H, nx=24)    # 上（台形に沿う）
    strip(-A, A, lambda x: -H, lambda x: BEZEL_HOLE_BOTTOM)                     # 下（穴は下の固定の板が見える高さまで）
    # 内枠の裏を囲う箱（上・下・左右・奥）
    t = 0.05
    for sz in (-1.0, 1.0):
        z0, z1 = sorted((sz * H, sz * (H + t)))
        y0 = bezel_y(H, BEZEL_T)
        mb.box((0.0, (y0 + ENCLOSURE_BACK_Y) * 0.5, (z0 + z1) * 0.5), (2 * (W + t), ENCLOSURE_BACK_Y - y0, t), "CP_Frame")
    for sx in (-1.0, 1.0):
        zs = [-H + 2 * H * k / 16 for k in range(17)]
        xs = (sx * W, sx * (W + t))
        front = [[Vector((x, bezel_y(z, BEZEL_T), z)) for x in xs] for z in zs]
        back = [[Vector((x, ENCLOSURE_BACK_Y, z)) for x in xs] for z in zs]
        mb.slab_grid(front, back, "CP_Frame")
    mb.box((0.0, ENCLOSURE_BACK_Y + t * 0.5, 0.0), (2 * (W + t), t, 2 * (H + t)), "CP_Frame")
    # 出入り口の左右の壁：内枠の裏から外殻まで（上下はハッチが通るので開けておく）
    for sx in (-1.0, 1.0):
        xs = (sx * APERTURE_HALF_W, sx * (APERTURE_HALF_W + 0.03))
        zs = [BEZEL_HOLE_BOTTOM + (APERTURE_HALF_H - BEZEL_HOLE_BOTTOM) * k / 12 for k in range(13)]
        inner = [[Vector((x, bezel_y(z, BEZEL_T), z)) for x in xs] for z in zs]
        outer = [[Vector((x, -math.sqrt(max(CP_R ** 2 - z * z, 0.0)), z)) for x in xs] for z in zs]
        mb.slab_grid(inner, outer, "CP_Frame")
        # 目隠し：外殻の外・ハッチの内側の半径で、出入り口の左右の縁に沿う帯
        x0, x1 = sx * (APERTURE_HALF_W - SEAL_W), sx * (APERTURE_HALF_W + 0.02)
        phi_bottom = math.degrees(math.asin(BEZEL_HOLE_BOTTOM / HATCH_R_IN))
        mb.cyl_panel(CP_R + SHELL_T * 0.5 + 0.002, HATCH_R_IN - 0.004, phi_bottom, APERTURE_PHI,
                     min(x0, x1), max(x0, x1), "CP_Frame")


def build_interior(mb):
    floor_z = -0.78
    # 床
    mb.box((0.0, 0.05, floor_z - 0.03), (2 * CP_HALF_LEN - 0.04, 1.45, 0.06), "CP_Frame")
    # 座席（前＝-Y を向く。背もたれは後ろへ 15° 倒す）
    recline = Quaternion(Vector((1, 0, 0)), math.radians(15.0))
    mb.box((0.0, 0.15, -0.46), (0.58, 0.55, 0.12), "CP_Seat")                     # 座面
    mb.box((0.0, 0.47, -0.02), (0.56, 0.14, 0.78), "CP_Seat", rot=recline)         # 背もたれ
    mb.box((0.0, 0.58, 0.48), (0.34, 0.12, 0.24), "CP_Seat", rot=recline)          # ヘッドレスト
    for sx in (-1.0, 1.0):
        mb.box((sx * 0.31, 0.45, 0.02), (0.08, 0.20, 0.62), "CP_Seat", rot=recline)  # 背もたれの横の張り出し
    mb.box((0.0, 0.25, -0.64), (0.30, 0.30, 0.24), "CP_Metal")                    # 座席の台
    # ひじ掛けと操縦桿（握りに緑の輪）
    for sx in (-1.0, 1.0):
        x = sx * 0.43
        mb.box((x, 0.08, -0.26), (0.13, 0.62, 0.09), "CP_Frame")
        mb.box((x, 0.08, -0.40), (0.10, 0.50, 0.20), "CP_Metal")
        mb.box((x, -0.20, -0.20), (0.12, 0.12, 0.04), "CP_Metal")                 # 操縦桿の台
        mb.rod((x, -0.20, -0.18), (x, -0.22, 0.02), 0.035, "CP_Seat")             # 握り
        mb.rod((x, -0.22, 0.02), (x, -0.225, 0.05), 0.042, "CP_Glow")             # 緑の輪
        mb.rod((x, -0.225, 0.05), (x, -0.23, 0.08), 0.03, "CP_Metal")             # 握りの頭
    # 足置き（格子の穴をまねた溝のペダル 2 枚、真ん中に光の帯）
    tilt = Quaternion(Vector((1, 0, 0)), math.radians(-40.0))
    for sx in (-1.0, 1.0):
        c = Vector((sx * 0.17, -0.62, -0.62))
        mb.box(c, (0.22, 0.04, 0.34), "CP_Metal", rot=tilt)
        for k in range(3):
            off = tilt @ Vector((0.0, -0.022, -0.10 + 0.10 * k))
            mb.box(c + off, (0.15, 0.01, 0.05), "CP_Frame", rot=tilt)
    mb.box((0.0, -0.64, -0.60), (0.06, 0.03, 0.30), "CP_Light", rot=tilt)
    mb.box((0.0, -0.60, -0.72), (0.56, 0.30, 0.10), "CP_Frame")                   # 足置きの台
    # 左右の壁ぎわの操作盤
    for sx in (-1.0, 1.0):
        mb.box((sx * 0.80, -0.10, -0.45), (0.30, 0.70, 0.18), "CP_Frame")
        mb.box((sx * 0.78, -0.10, -0.355), (0.20, 0.55, 0.01), "CP_Screen")
        for k in range(3):
            mb.box((sx * 0.74, -0.32 + 0.18 * k, -0.345), (0.04, 0.04, 0.012), "CP_Glow")


def meet_z(x):
    """ハッチと下の固定の板の合わせ目。中央が台形に上へ出る（スケッチ：下の板の出っ張りにハッチの切り欠きがはまる）"""
    return aperture_edge_z(min(abs(x), APERTURE_HALF_W), top=False)


def curved_plate(mb, z_lo, z_hi, mat, nx=48, nz=24, r_in=None, r_out=None):
    """ハッチと同じ半径の曲面の板。x ごとに z_lo(x)..z_hi(x) の範囲（高さ→角度は板の中央の半径で）"""
    r_in = r_in if r_in is not None else HATCH_R_IN
    r_out = r_out if r_out is not None else HATCH_R_OUT
    rm = (HATCH_R_IN + HATCH_R_OUT) * 0.5
    xs = [-HATCH_HALF_W + 2 * HATCH_HALF_W * k / nx for k in range(nx + 1)]

    def phi(z):
        return math.degrees(math.asin(max(-1.0, min(1.0, z / rm))))

    def grid(r):
        return [[cyl(r, phi(z_lo(x)) + (phi(z_hi(x)) - phi(z_lo(x))) * j / nz, x) for x in xs] for j in range(nz + 1)]

    mb.slab_grid(grid(r_in), grid(r_out), mat)


def build_lower_plate(mb):
    curved_plate(mb, lambda x: LOWER_PLATE_BOTTOM, meet_z, "HT_Navy")
    # 合わせ目の少し下に金の帯（台形に沿う）
    curved_plate(mb, lambda x: meet_z(x) - 0.09, lambda x: meet_z(x) - 0.05, "HT_Gold",
                 nz=2, r_in=HATCH_R_OUT, r_out=HATCH_R_OUT + 0.012)


def build_cockpit(mats):
    mb = MeshBuilder()
    build_shell(mb)
    build_bezel(mb)
    build_lower_plate(mb)
    build_interior(mb)
    return mb.build("HubMech_Cockpit", mats)


def build_hatch(mats):
    """横に倒した円柱の一部の曲面の板。閉じた位置で作る（原点＝回転の軸）。
    下の縁は合わせ目（meet_z）どおりの台形の切り欠きで、下の固定の板の出っ張りにはまる"""
    mb = MeshBuilder()
    top = lambda x: APERTURE_HALF_H + 0.13
    curved_plate(mb, meet_z, top, "HT_Navy")
    # 外側：上の縁と、下の縁（切り欠きに沿う）の少し内側に金の帯
    curved_plate(mb, lambda x: meet_z(x) + 0.05, lambda x: meet_z(x) + 0.09, "HT_Gold",
                 nz=2, r_in=HATCH_R_OUT, r_out=HATCH_R_OUT + 0.012)
    curved_plate(mb, lambda x: top(x) - 0.09, lambda x: top(x) - 0.05, "HT_Gold",
                 nz=2, r_in=HATCH_R_OUT, r_out=HATCH_R_OUT + 0.012)
    # 内側：一段へこんだ板（参考の穴あきの板）
    rm = (HATCH_R_IN + HATCH_R_OUT) * 0.5
    lo = math.degrees(math.asin(meet_z(0.0) / rm)) + 10.0
    hi = math.degrees(math.asin(top(0.0) / rm)) - 8.0
    mb.cyl_panel(HATCH_R_IN - 0.015, HATCH_R_IN, lo, hi, -HATCH_HALF_W + 0.14, HATCH_HALF_W - 0.14, "HT_Inner")
    return mb.build("HubMech_Hatch", mats)


def place_cockpit_parts(cockpit, hatch, scale):
    """コクピットのローカル（m）を、拡大後の本体の座標へ置く"""
    center = COCKPIT_CENTER * scale
    cockpit.location = center
    hatch.location = center
    return center


def set_hatch_open(hatch, open_ratio):
    hatch.rotation_euler = (math.radians(-HATCH_OPEN_DEG * open_ratio), 0.0, 0.0)


# ============================================================
# 書き出し（glTF。cook がワールド変換を頂点へ焼くので、ハッチは書き出す間だけ位置・回転を 0 に戻す）
# ============================================================
TEXTURE_SIZE = 2048   # 元は 4K。リポジトリへ push できる大きさに縮める


def save_textures_as_png(out_dir):
    """glb の中の JPEG を 2K の PNG で書き出し直す（cook は PNG だけを DDS へ変換する）。法線マップは名前に NormalMap を含める"""
    for img in list(bpy.data.images):
        if img.size[0] == 0:
            continue
        if img.size[0] > TEXTURE_SIZE:
            img.scale(TEXTURE_SIZE, TEXTURE_SIZE)
        name = img.name.replace("_normal", "_NormalMap")
        path = out_dir / f"{name}.png"
        img.filepath_raw = str(path)
        img.file_format = "PNG"
        img.save()
        img.name = name
        print(f"[rig_hub_mech] texture {path}")


def export_object(obj, path, reset_transform=False):
    saved = (obj.location.copy(), obj.rotation_euler.copy())
    if reset_transform:
        obj.location = (0, 0, 0)
        obj.rotation_euler = (0, 0, 0)
    bpy.ops.object.select_all(action="DESELECT")
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    bpy.ops.export_scene.gltf(filepath=str(path), export_format="GLTF_SEPARATE", use_selection=True,
                              export_yup=True, export_apply=True, export_animations=False,
                              export_image_format="AUTO")
    obj.location, obj.rotation_euler = saved
    flip_texcoord_v(path)
    write_pbr_factors(path)
    print(f"[rig_hub_mech] exported {path}")


def flip_texcoord_v(gltf_path):
    """書き出した glTF の TEXCOORD_0 を v → 1 - v にする。
    cook（tools/Python/cook_assets.py）は glTF の V を必ず反転するが、Blender の glTF の UV は
    すでに左上が原点で正しい。そのままだとアトラスの島が別の場所を拾って迷彩のようになるので、
    gen_terrain.py などと同じく「cook の反転の逆」を書いておく"""
    gltf_path = Path(gltf_path)
    gltf = json.loads(gltf_path.read_text(encoding="utf-8"))
    accessor_ids = {prim["attributes"]["TEXCOORD_0"]
                    for mesh in gltf.get("meshes", []) for prim in mesh["primitives"]
                    if "TEXCOORD_0" in prim.get("attributes", {})}
    if not accessor_ids:
        return
    bin_path = gltf_path.parent / gltf["buffers"][0]["uri"]
    data = bytearray(bin_path.read_bytes())
    for ai in accessor_ids:
        acc = gltf["accessors"][ai]
        assert acc["componentType"] == 5126 and acc["type"] == "VEC2", acc
        view = gltf["bufferViews"][acc["bufferView"]]
        assert view.get("buffer", 0) == 0
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
    print(f"[rig_hub_mech] flipped TEXCOORD_0 V for cook ({gltf_path.name})")


def write_pbr_factors(gltf_path):
    """metallicRoughnessTexture を持つ material に、テクスチャの中央値を metallicFactor / roughnessFactor として書く。
    エンジンはこのテクスチャを読まず、cook は係数が無いと glTF 既定の 1.0（完全な金属）を .mat に入れるため、
    Tripo の機体が暗く青灰色にくすむ（テクスチャの実際の金属度はほぼ 0）。アトラスの空き（G=B=0）は除く"""
    import numpy as np
    gltf_path = Path(gltf_path)
    gltf = json.loads(gltf_path.read_text(encoding="utf-8"))
    changed = False
    for mat in gltf.get("materials", []):
        pbr = mat.get("pbrMetallicRoughness", {})
        tex = pbr.get("metallicRoughnessTexture")
        if tex is None:
            continue
        uri = gltf["images"][gltf["textures"][tex["index"]]["source"]]["uri"]
        img = bpy.data.images.load(str(gltf_path.parent / uri), check_existing=True)
        px = np.empty(img.size[0] * img.size[1] * img.channels, dtype=np.float32)
        img.pixels.foreach_get(px)
        px = px.reshape(-1, img.channels)
        used = (px[:, 1] > 0.01) | (px[:, 2] > 0.01)
        if not used.any():
            continue
        pbr["roughnessFactor"] = round(float(np.median(px[used, 1])), 3)
        pbr["metallicFactor"] = round(float(np.median(px[used, 2])), 3)
        mat["pbrMetallicRoughness"] = pbr
        changed = True
        print(f"[rig_hub_mech] {mat.get('name')}: metallicFactor={pbr['metallicFactor']} "
              f"roughnessFactor={pbr['roughnessFactor']} (from {uri})")
    if changed:
        gltf_path.write_text(json.dumps(gltf, indent=2), encoding="utf-8")


# ============================================================
# プレビュー
# ============================================================
def setup_render(width, height):
    sc = bpy.context.scene
    sc.render.engine = "BLENDER_WORKBENCH"
    sc.render.resolution_x = width
    sc.render.resolution_y = height
    sc.display.shading.light = "STUDIO"
    sc.display.shading.show_cavity = True
    sc.display.render_aa = "8"
    sc.view_settings.view_transform = "Standard"
    world = bpy.data.worlds.new("Bg")
    world.color = (0.75, 0.76, 0.78)
    sc.world = world
    cam = bpy.data.objects.new("Camera", bpy.data.cameras.new("Camera"))
    sc.collection.objects.link(cam)
    sc.camera = cam
    return cam


def look(cam, loc, target, ortho=None, lens=50.0):
    cam.location = loc
    cam.rotation_euler = (Vector(target) - Vector(loc)).to_track_quat("-Z", "Y").to_euler()
    if ortho is not None:
        cam.data.type = "ORTHO"
        cam.data.ortho_scale = ortho
    else:
        cam.data.type = "PERSP"
        cam.data.lens = lens


def render(path):
    bpy.context.scene.render.filepath = str(path)
    bpy.ops.render.render(write_still=True)
    print(f"[rig_hub_mech] wrote {path}")


def add_ground():
    bpy.ops.mesh.primitive_plane_add(size=4.0, location=(0.0, 0.0, 0.0))
    g = bpy.context.active_object
    m = bpy.data.materials.new("Ground")
    m.diffuse_color = (0.45, 0.45, 0.47, 1.0)
    g.data.materials.append(m)


def preview_groups(groups, out_dir, cam):
    """グループの確認用に単色のマテリアルへ一時的に差し替えて撮り、元へ戻す"""
    sc = bpy.context.scene
    sc.display.shading.color_type = "MATERIAL"
    saved = {o.name: list(o.data.materials) for objs in groups.values() for o in objs}
    for name, objs in groups.items():
        m = bpy.data.materials.new("Group_" + name)
        m.diffuse_color = (*GROUP_COLORS[name], 1.0)
        for o in objs:
            o.data.materials.clear()
            o.data.materials.append(m)
    look(cam, (0.0, -3.0, 0.49), (0.0, 0.0, 0.49), ortho=1.0)
    render(out_dir / "groups_front.png")
    look(cam, (-3.0, 0.0, 0.49), (0.0, 0.0, 0.49), ortho=1.0)
    render(out_dir / "groups_side.png")
    look(cam, (0.0, 3.0, 0.49), (0.0, 0.0, 0.49), ortho=1.0)
    render(out_dir / "groups_back.png")
    for objs in groups.values():
        for o in objs:
            o.data.materials.clear()
            for m in saved[o.name]:
                o.data.materials.append(m)


def arg_path(argv, name):
    return Path(argv[argv.index(name) + 1]) if name in argv else None


def main():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    src = arg_path(argv, "--src") or Path("Assets/Models/HubMech/HubMech.glb")
    preview_dir = arg_path(argv, "--preview")
    save_path = arg_path(argv, "--save")
    export_dir = arg_path(argv, "--export")

    bpy.ops.wm.read_factory_settings(use_empty=True)
    meshes = import_source(src.resolve())
    groups = group_parts(meshes)
    report(groups)
    scale = BODY_HEIGHT_M / body_top_z(groups)
    print(f"[rig_hub_mech] scale {scale:.4f} (1 unit = {scale:.2f} m)")

    cam = None
    if preview_dir is not None:
        preview_dir = preview_dir.resolve()
        preview_dir.mkdir(parents=True, exist_ok=True)
        cam = setup_render(1200, 1200)
        preview_groups(groups, preview_dir, cam)

    arm = build_armature()
    mesh = join_with_weights(groups, arm)
    pose_hub(arm)
    bake_pose(mesh, arm)
    mesh.data.transform(Matrix.Scale(scale, 4))

    mats = cp_materials()
    cockpit = build_cockpit(mats)
    hatch = build_hatch(mats)
    center = place_cockpit_parts(cockpit, hatch, scale)
    print(f"[rig_hub_mech] cockpit center (Blender, m) = ({center.x:.3f}, {center.y:.3f}, {center.z:.3f})")
    tris = sum(len(p.vertices) - 2 for o in (cockpit, hatch) for p in o.data.polygons)
    print(f"[rig_hub_mech] cockpit+hatch {tris} tris")

    if save_path is not None:
        save_path.parent.mkdir(parents=True, exist_ok=True)
        bpy.ops.wm.save_as_mainfile(filepath=str(save_path.resolve()))

    if export_dir is not None:
        export_dir = export_dir.resolve()
        export_dir.mkdir(parents=True, exist_ok=True)
        save_textures_as_png(export_dir)
        export_object(mesh, export_dir / "HubMech.gltf")
        export_object(cockpit, export_dir / "HubMech_Cockpit.gltf")
        set_hatch_open(hatch, 0.0)
        export_object(hatch, export_dir / "HubMech_Hatch.gltf", reset_transform=True)

    if preview_dir is not None:
        render_previews(preview_dir, cam, scale, center, hatch)


def render_previews(out_dir, cam, scale, center, hatch):
    bpy.ops.mesh.primitive_plane_add(size=80.0, location=(0.0, 0.0, 0.0))
    g = bpy.context.active_object
    gm = bpy.data.materials.new("Ground")
    gm.diffuse_color = (0.45, 0.45, 0.47, 1.0)
    g.data.materials.append(gm)
    sc = bpy.context.scene
    sc.display.shading.color_type = "TEXTURE"
    s = scale
    set_hatch_open(hatch, 1.0)
    look(cam, (0.0, -3.0 * s, 0.49 * s), (0.0, 0.0, 0.49 * s), ortho=1.0 * s)
    render(out_dir / "pose_front.png")
    look(cam, (-0.45 * s, -0.95 * s, 0.80 * s), (-0.02 * s, -0.12 * s, 0.56 * s), lens=50.0)
    render(out_dir / "pose_hand.png")
    # コクピットまわり（外から：開・途中・閉）
    look(cam, center + Vector((-2.5, -7.0, 1.2)), center + Vector((0, 0, -0.2)), lens=35.0)
    render(out_dir / "hatch_open.png")
    set_hatch_open(hatch, 0.45)
    render(out_dir / "hatch_half.png")
    set_hatch_open(hatch, 0.0)
    render(out_dir / "hatch_closed.png")
    # 座席からの一人称（開・閉・足元・上）
    eye = center + EYE
    sc.render.resolution_x, sc.render.resolution_y = 1280, 720
    set_hatch_open(hatch, 1.0)
    look(cam, eye, eye + Vector((0, -1.0, -0.08)), lens=16.0)
    render(out_dir / "fps_open.png")
    set_hatch_open(hatch, 0.0)
    render(out_dir / "fps_closed.png")
    look(cam, eye, eye + Vector((0, -0.6, -1.0)), lens=16.0)
    render(out_dir / "fps_down.png")
    set_hatch_open(hatch, 1.0)
    look(cam, eye, eye + Vector((0, -0.3, 1.0)), lens=16.0)
    render(out_dir / "fps_up.png")


main()
