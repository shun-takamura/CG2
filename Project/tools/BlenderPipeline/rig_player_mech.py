"""自機（Tripo で生成、部位分割して名前を付けた glb）の掃除・名前の修正・体と羽根の分離・ボーン入れ。

使い方（Project/ で実行）:
    blender -b --factory-startup -P tools/BlenderPipeline/rig_player_mech.py -- [オプション]
        --src <glb>            元のモデル（既定 Assets/Models/PlayerMech/PlayerMech.glb）
        --save <パス.blend>    結果を .blend で保存（テクスチャは中に埋め込む）。Blender で開いてボーンを入れられる
        --preview <フォルダ>   部位の色分け（正面・背面）と、羽根の部位の名前を描いた背面の PNG。
                               --rig と一緒なら T ポーズの前後と、羽根をたたんだ確認用の PNG も
        --src-blend <.blend>   組み上げたモデル（パーツを 1 つずつ生成して Blender で組んだもの）から体のボーンを入れ、
                               関節の部品を付けて --save へ。テクスチャは 2K・左右で 1 つ・反射は LowerLeg にそろえ、
                               画像は --save の隣の textures/ に PNG で置く。羽根は別のモデル（扱わない）
        --wings <体の .blend>  羽根：--src の GLB（前のモデル）から羽根を取り出し、組み上げた体（PlayerModel_rig.blend）の
                               大きさ・背中の位置に合わせてボーンを入れ、--save へ（WING_FIT で倍率とずれを直せる）
        --rig                  体と羽根を分けてボーンを入れ、色を塗り分け、体を腕・脚・胴に分けた編集用の .blend にする（下）。
                               --save に既にあるファイルは上書きしない（手で直した分が消えるので。上書きは --force）
        --finish <編集用.blend> 手で直した編集用の .blend を PlayerBody にまとめ直して --save（既定 PlayerMech_rig.blend）へ
        --mirror-left-arm <編集用.blend>  その .blend の右腕（Body_RightArm）を左腕の鏡写しに置き換えて上書き保存。
                               右腕のボーンも左腕の鏡写しの位置にする。ほかの手の修正は残る
        --body-clips <体の .blend>  体（PlayerModel_rig.blend）にスタート演出の姿勢のクリップ（BODY_CLIPS）を付け、
                               --preview で確認画像、--export <フォルダ> で PlayerModel.gltf を書き出す（.blend は上書きしない）
        --export-wings <羽根の .blend>  羽根（PlayerWings_rig.blend）を Deploy / Stow のクリップごと --export <フォルダ> へ
                               PlayerWings.gltf で書き出す

流れ：--rig --save Assets/Models/PlayerMech/PlayerMech_edit.blend → Blender で穴を埋める・凹凸を付ける・マテリアルを塗る
      → --finish Assets/Models/PlayerMech/PlayerMech_edit.blend
色：テクスチャ・法線マップはやめ、マテリアル MechWhite / MechGold / MechBlue（機体）と MechBlack / MechGold（関節）。
    最初は機体を全部白にする（金・青は手で塗る）。新しく作った面にはマテリアルを割り当てるだけでよい

ボーン（--rig）:
    体  … PlayerBody ＋ PlayerBodyRig。Mixamo と同じ名前・親子（mixamorig:Hips〜Head・腕・脚。指は無い）。
           部位ごとにウェイト 1.0。関節は隣り合う部位が接している所の中心。Tripo のポーズは T ポーズ
           （腕は横・脚はまっすぐ下）に直して、それを基本の姿勢にした
    羽根 … PlayerWings ＋ PlayerWingRig（体と同じ原点の別のモデル。エンジンでは体の mixamorig:Spine2 に付ける想定）。
           WingRoot → 片側の翼ごとに関節（上の翼 Arm→Fore→Hand＝列 0 / 列 1 / 列 2・3、下の翼 Arm→Fore＝列 0 / 列 1）
           → 部位ごとのボーン（部位と同じ名前、ウェイト 1.0、列の関節から部位の先端へ）

部位の名前（Tripo で付けたもの）:
    体   … Head / Neck / Chest / Torso / LowerBack / {Left,Right}{Shoulder,UpperArm,Forearm,Hand,Thigh,LowerLeg,Leg}
    羽根 … {Left,Right}{Upper,Lower}Wing_<列><行>。列＝付け根から何列目、行＝上から何行目（どちらも 0 から）。
           例：RightUpperWing_00 は付け根のいちばん上、_02 は付け根のいちばん下、_21 は付け根から 3 列目の 2 行目
    綴りの誤り（Letf / Sholder）はここで直す（Tripo で直すと書き出し直しになるため）。

掃除（羽根は同じ側の羽根どうし、体は体の部位どうし）：Tripo で部位を塗り分けたときの塗り漏らしは、たいてい本体から離れた小さな「島」（つながったメッシュの塊）として
別の部位に混ざっている。羽根の部位ごとに島に分け、自分の部位の本体（いちばん大きい島）より別の羽根の部位（同じ側）の方がずっと近い島を、
いちばん近い部位へ移す（消すと羽根に穴が開くので消さない）。部位の大部分を占める大きな島は、意図して分けた層かも
しれないので動かさず報告だけする。

座標（Blender）: X=左右（+X がモデルの左）/ Y=奥（正面は -Y 側から見る）/ Z=上。元のモデルは足元 Z=0・全高 ≈0.55。
"""

import colorsys
import hashlib
import json
import struct
import sys
from pathlib import Path

import bmesh
import bpy
import numpy as np
from mathutils import Matrix, Quaternion, Vector
from mathutils.kdtree import KDTree

# ============================================================
# 名前の修正（部分文字列の置き換え）
# ============================================================
RENAMES = [
    ("Letf", "Left"),
    ("Sholder", "Shoulder"),
]

# ============================================================
# 掃除の基準
# ============================================================
WELD_EPS = 1e-5            # 同じ位置の頂点（UV の継ぎ目で分かれている）を 1 つとみなす距離
MOVE_RATIO = 0.5           # 別の部位までの平均距離が、自分の部位の本体までの平均距離のこの割合未満なら移す
MIN_OWN_DISTANCE = 0.003   # 自分の部位の本体までの平均距離がこれ未満なら、くっついているので移さない
MAX_ISLAND_SHARE = 0.25    # 部位の頂点のこの割合以上の島は動かさない（報告だけ）
SAMPLE_POINTS = 200        # 距離を測るときに島から取る点の数（多い島は間引く）
MOVE_ATTR = "move_target"  # 移す先の番号を頂点に書いておく属性（separate / join を通っても残る）

WING_MARK = "Wing_"


def log(msg):
    print(f"[rig_player_mech] {msg}")


# ============================================================
# 読み込みと名前
# ============================================================
def import_source(path):
    bpy.ops.import_scene.gltf(filepath=str(path))
    return [o for o in bpy.context.scene.objects if o.type == "MESH"]


def fix_names(objs):
    for o in objs:
        new = o.name
        for a, b in RENAMES:
            new = new.replace(a, b)
        if new != o.name:
            log(f"rename {o.name} -> {new}")
            o.name = new
        o.data.name = o.name


def wing_side(name):
    return "Left" if name.startswith("Left") else "Right"


# ============================================================
# 島と距離
# ============================================================
def world_vertices(o):
    me = o.data
    co = np.empty(len(me.vertices) * 3)
    me.vertices.foreach_get("co", co)
    co = co.reshape(-1, 3)
    m = np.array(o.matrix_world)
    return co @ m[:3, :3].T + m[:3, 3]


def island_labels(o, co):
    """頂点ごとの島の番号（同じ位置の頂点は 1 つとみなしてつなぐ）"""
    me = o.data
    me.calc_loop_triangles()
    tri = np.empty(len(me.loop_triangles) * 3, dtype=np.int64)
    me.loop_triangles.foreach_get("vertices", tri)
    _, weld = np.unique(np.round(co / WELD_EPS).astype(np.int64), axis=0, return_inverse=True)
    weld = weld.ravel()
    parent = list(range(int(weld.max()) + 1))

    def find(a):
        while parent[a] != a:
            parent[a] = parent[parent[a]]
            a = parent[a]
        return a

    for t in weld[tri.reshape(-1, 3)]:
        a = find(t[0])
        parent[find(t[1])] = a
        parent[find(t[2])] = a
    roots = np.array([find(i) for i in range(len(parent))])
    _, labels = np.unique(roots[weld], return_inverse=True)
    return labels.ravel()


def make_tree(points):
    tree = KDTree(len(points))
    for i, p in enumerate(points):
        tree.insert(p, i)
    tree.balance()
    return tree


def mean_distance(tree, points):
    step = max(1, len(points) // SAMPLE_POINTS)
    return float(np.mean([tree.find(p)[2] for p in points[::step]]))


def plan_moves(wings, same_group):
    """移す島を決める（全部の部位の元の形で決めてから動かす）。same_group(a, b) が真の部位どうしだけで移す。
    戻り値：部位 → 頂点ごとの移す先（-1 は残す）"""
    names = sorted(wings)
    coords = {n: world_vertices(wings[n]) for n in names}
    trees = {n: make_tree(coords[n]) for n in names}
    plans = {}
    kept_large = []
    moved = []
    for name in names:
        o = wings[name]
        co = coords[name]
        labels = island_labels(o, co)
        sizes = np.bincount(labels)
        target = np.full(len(co), -1, dtype=np.int64)
        order = np.argsort(-sizes)
        # 部位の本体（いちばん大きい島）までの距離で比べる。「残り全部」と比べると、
        # 翼の先に固まった塗り漏らしどうしが互いに近いので残ってしまう
        body = co[labels == order[0]]
        body_tree = make_tree(body[:: max(1, len(body) // 20000)])
        for island in order[1:]:   # 本体は残す
            mask = labels == island
            pts = co[mask]
            d_own = mean_distance(body_tree, pts)
            best = None
            for other in names:
                if other == name or not same_group(name, other):
                    continue
                d = mean_distance(trees[other], pts)
                if best is None or d < best[1]:
                    best = (other, d)
            if best is None or d_own <= MIN_OWN_DISTANCE or best[1] >= d_own * MOVE_RATIO:
                continue
            if mask.sum() >= MAX_ISLAND_SHARE * len(co):
                kept_large.append((name, int(mask.sum()), len(co), best[0], d_own, best[1]))
                continue
            target[mask] = names.index(best[0])
            moved.append((name, best[0], int(mask.sum())))
        plans[name] = target
    return names, plans, moved, kept_large


def select_by_attr(o, value):
    """属性 MOVE_ATTR が value の頂点と面だけを選ぶ（オブジェクトモードで）"""
    me = o.data
    vals = np.empty(len(me.vertices), dtype=np.int32)
    me.attributes[MOVE_ATTR].data.foreach_get("value", vals)
    sel = vals == value
    me.vertices.foreach_set("select", sel)
    ev = np.empty(len(me.edges) * 2, dtype=np.int64)
    me.edges.foreach_get("vertices", ev)
    me.edges.foreach_set("select", sel[ev.reshape(-1, 2)].all(axis=1))
    loops = np.empty(len(me.loops), dtype=np.int64)
    me.loops.foreach_get("vertex_index", loops)
    starts = np.empty(len(me.polygons), dtype=np.int64)
    me.polygons.foreach_get("loop_start", starts)
    poly_sel = np.logical_and.reduceat(sel[loops], starts) if len(starts) else np.zeros(0, bool)
    me.polygons.foreach_set("select", poly_sel)
    return int(sel.sum())


def separate_selected(o):
    """選んだ所を別のオブジェクトへ分けて、それを返す"""
    before = set(bpy.data.objects)
    bpy.ops.object.select_all(action="DESELECT")
    bpy.context.view_layer.objects.active = o
    o.select_set(True)
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.separate(type="SELECTED")
    bpy.ops.object.mode_set(mode="OBJECT")
    new = [x for x in bpy.data.objects if x not in before]
    return new[0] if new else None


def join_into(target, piece):
    bpy.ops.object.select_all(action="DESELECT")
    piece.select_set(True)
    target.select_set(True)
    bpy.context.view_layer.objects.active = target
    bpy.ops.object.join()


def apply_moves(wings, names, plans):
    # 1) 移す先を頂点の属性に書く
    for name in names:
        me = wings[name].data
        attr = me.attributes.new(MOVE_ATTR, "INT", "POINT")
        attr.data.foreach_set("value", plans[name].astype(np.int32))
    # 2) 各部位から出ていく島をまとめて切り離す（ここで元の部位の頂点の番号が変わるので、先に全部切り離す）
    pieces = []
    for name in names:
        o = wings[name]
        vals = plans[name]
        if (vals >= 0).sum() == 0:
            continue
        me = o.data
        sel = vals >= 0
        me.vertices.foreach_set("select", sel)
        ev = np.empty(len(me.edges) * 2, dtype=np.int64)
        me.edges.foreach_get("vertices", ev)
        me.edges.foreach_set("select", sel[ev.reshape(-1, 2)].all(axis=1))
        loops = np.empty(len(me.loops), dtype=np.int64)
        me.loops.foreach_get("vertex_index", loops)
        starts = np.empty(len(me.polygons), dtype=np.int64)
        me.polygons.foreach_get("loop_start", starts)
        me.polygons.foreach_set("select", np.logical_and.reduceat(sel[loops], starts))
        piece = separate_selected(o)
        if piece:
            pieces.append(piece)
    # 3) 切り離した塊を、移す先ごとに分けて先の部位へつなぐ
    for piece in pieces:
        for t in range(len(names)):
            if not piece.data.vertices:
                break
            if select_by_attr(piece, t) == 0:
                continue
            if select_by_attr(piece, t) == len(piece.data.vertices):
                join_into(wings[names[t]], piece)
                piece = None
                break
            part = separate_selected(piece)
            if part:
                join_into(wings[names[t]], part)
        if piece is not None and piece.name in bpy.data.objects and not piece.data.vertices:
            bpy.data.objects.remove(piece)
    # 4) 属性を消す
    for name in names:
        me = wings[name].data
        if MOVE_ATTR in me.attributes:
            me.attributes.remove(me.attributes[MOVE_ATTR])


def report_extents(wings):
    log("羽根の部位の範囲（掃除の後、Blender 座標）:")
    for name in sorted(wings):
        co = world_vertices(wings[name])
        mn, mx = co.min(0), co.max(0)
        log(f"  {name:20s} v={len(co):6d} x[{mn[0]:+.3f},{mx[0]:+.3f}] y[{mn[1]:+.3f},{mx[1]:+.3f}] z[{mn[2]:.3f},{mx[2]:.3f}]")


# ============================================================
# プレビュー
# ============================================================
def part_color(name):
    h = int(hashlib.md5(name.encode()).hexdigest()[:8], 16)
    r, g, b = colorsys.hsv_to_rgb((h % 360) / 360.0, 0.55 + (h >> 9) % 40 / 100.0, 0.95)
    return (r, g, b, 1.0)


def setup_render(width, height):
    sc = bpy.context.scene
    sc.render.engine = "BLENDER_WORKBENCH"
    sc.render.resolution_x = width
    sc.render.resolution_y = height
    sc.display.shading.light = "STUDIO"
    sc.display.shading.color_type = "OBJECT"
    sc.display.shading.show_cavity = True
    sc.display.render_aa = "8"
    sc.view_settings.view_transform = "Standard"
    world = bpy.data.worlds.new("Bg")
    world.color = (0.30, 0.30, 0.32)
    sc.world = world
    cam = bpy.data.objects.new("PreviewCamera", bpy.data.cameras.new("PreviewCamera"))
    sc.collection.objects.link(cam)
    sc.camera = cam
    cam.data.type = "ORTHO"
    return cam


def look(cam, loc, target, ortho):
    cam.location = loc
    cam.rotation_euler = (Vector(target) - Vector(loc)).to_track_quat("-Z", "Y").to_euler()
    cam.data.ortho_scale = ortho


def render(path):
    bpy.context.scene.render.filepath = str(path)
    bpy.ops.render.render(write_still=True)
    log(f"wrote {path}")


def add_labels(wings, toward_y):
    """羽根の部位の名前（行と列の番号）を部位の中心の手前に置く。toward_y はカメラのある側（+1 / -1）"""
    labels = []
    for name, o in wings.items():
        co = world_vertices(o)
        center = co.mean(0)
        curve = bpy.data.curves.new("Label_" + name, "FONT")
        curve.body = name.split("_")[-1]
        curve.size = 0.018
        curve.align_x = "CENTER"
        curve.align_y = "CENTER"
        label = bpy.data.objects.new("Label_" + name, curve)
        label.location = (center[0], co[:, 1].min() - 0.05 if toward_y < 0 else co[:, 1].max() + 0.05, center[2])
        # 文字は XY 面に置かれ +Z を向くので、カメラの方へ起こす
        label.rotation_euler = (np.pi / 2, 0.0, 0.0 if toward_y < 0 else np.pi)
        label.color = (1.0, 1.0, 1.0, 1.0) if name.startswith("Right") else (1.0, 1.0, 0.3, 1.0)
        bpy.context.scene.collection.objects.link(label)
        labels.append(label)
    return labels


def preview(objs, wings, out_dir):
    out_dir.mkdir(parents=True, exist_ok=True)
    cam = setup_render(1600, 1000)
    for o in objs:
        o.color = part_color(o.name)
    center = (0.0, 0.0, 0.30)
    look(cam, (0.0, -3.0, center[2]), center, 1.1)
    render(out_dir / "parts_front.png")
    look(cam, (0.0, 3.0, center[2]), center, 1.1)
    render(out_dir / "parts_back.png")
    labels = add_labels(wings, +1)
    render(out_dir / "wings_back_labels.png")
    for label in labels:
        bpy.data.objects.remove(label)
    bpy.data.objects.remove(cam)
    for o in objs:
        o.color = (1.0, 1.0, 1.0, 1.0)


# ============================================================
# ボーン（--rig）
# ============================================================
# 体の部位 → Mixamo のボーン。Mixamo のモーションと player.json の武器（mixamorig:RightHand）をそのまま使うため、
# 名前と親子は Mixamo と同じにする（指・目などは無いので作らない）。部位ごとにウェイト 1.0（リジッド）
# 長さの数値（判定の距離・関節の部品の大きさ・確認用のカメラ）は最初のモデル（全高 0.55）で決めたもの。
# 別の大きさのモデル（--src-blend の組み上げたモデルは全高 1.69）では全高の比 UNIT を掛ける
BASE_HEIGHT = 0.55
UNIT = 1.0


def u(x):
    """最初のモデルの大きさで書いた長さを、今のモデルの大きさにする"""
    return x * UNIT


def uv3(v):
    return tuple(c * UNIT for c in v)


MX = "mixamorig:"
BODY_PART_BONE = {
    "LowerBack": "Hips",
    "Torso": "Spine",
    "Chest": "Spine2",
    "Neck": "Neck",
    "Head": "Head",
    "RightShoulder": "RightShoulder",
    "RightUpperArm": "RightArm",
    "RightForearm": "RightForeArm",
    "RightHand": "RightHand",
    "RightThigh": "RightUpLeg",
    "RightLowerLeg": "RightLeg",
    "RightLeg": "RightFoot",
    "LeftShoulder": "LeftShoulder",
    "LeftUpperArm": "LeftArm",
    "LeftForearm": "LeftForeArm",
    "LeftHand": "LeftHand",
    "LeftThigh": "LeftUpLeg",
    "LeftLowerLeg": "LeftLeg",
    "LeftLeg": "LeftFoot",
}
# 関節＝隣り合う部位が接している所の中心：(ボーン, 親の部位, 子の部位)
BODY_JOINTS = [
    ("Spine", "LowerBack", "Torso"),
    ("Spine2", "Torso", "Chest"),
    ("Neck", "Chest", "Neck"),
    ("Head", "Neck", "Head"),
    ("RightShoulder", "Chest", "RightShoulder"),
    ("RightArm", "RightShoulder", "RightUpperArm"),
    ("RightForeArm", "RightUpperArm", "RightForearm"),
    ("RightHand", "RightForearm", "RightHand"),
    ("RightUpLeg", "LowerBack", "RightThigh"),
    ("RightLeg", "RightThigh", "RightLowerLeg"),
    ("RightFoot", "RightLowerLeg", "RightLeg"),
    ("LeftShoulder", "Chest", "LeftShoulder"),
    ("LeftArm", "LeftShoulder", "LeftUpperArm"),
    ("LeftForeArm", "LeftUpperArm", "LeftForearm"),
    ("LeftHand", "LeftForearm", "LeftHand"),
    ("LeftUpLeg", "LowerBack", "LeftThigh"),
    ("LeftLeg", "LeftThigh", "LeftLowerLeg"),
    ("LeftFoot", "LeftLowerLeg", "LeftLeg"),
]
BODY_PARENT = {
    "Hips": None, "Spine": "Hips", "Spine1": "Spine", "Spine2": "Spine1", "Neck": "Spine2", "Head": "Neck",
    "RightShoulder": "Spine2", "RightArm": "RightShoulder", "RightForeArm": "RightArm", "RightHand": "RightForeArm",
    "LeftShoulder": "Spine2", "LeftArm": "LeftShoulder", "LeftForeArm": "LeftArm", "LeftHand": "LeftForeArm",
    "RightUpLeg": "Hips", "RightLeg": "RightUpLeg", "RightFoot": "RightLeg", "RightToeBase": "RightFoot",
    "LeftUpLeg": "Hips", "LeftLeg": "LeftUpLeg", "LeftFoot": "LeftLeg", "LeftToeBase": "LeftFoot",
}
# T ポーズで向けるボーンの向き（Blender 座標。正面は -Y、+X がモデルの左）。親から順に回す
T_POSE_DIRS = [
    ("RightArm", (-1.0, 0.0, 0.0)), ("RightForeArm", (-1.0, 0.0, 0.0)), ("RightHand", (-1.0, 0.0, 0.0)),
    ("LeftArm", (1.0, 0.0, 0.0)), ("LeftForeArm", (1.0, 0.0, 0.0)), ("LeftHand", (1.0, 0.0, 0.0)),
    ("RightUpLeg", (0.0, 0.0, -1.0)), ("RightLeg", (0.0, 0.0, -1.0)), ("RightFoot", (0.0, -1.0, -0.35)),
    ("LeftUpLeg", (0.0, 0.0, -1.0)), ("LeftLeg", (0.0, 0.0, -1.0)), ("LeftFoot", (0.0, -1.0, -0.35)),
]
# 右脚は左脚の鏡写しで作り直す（2026-10-16 ユーザー決定）。元のポーズで右脚だけ後ろへ大きく曲がっていて、
# Tripo の部位の切れ目と関節の位置が左右でずれ、T ポーズで右脚が 2.5cm 短くなったため。鏡の面は x = 0
# 右腕も左腕の鏡写し（2026-10-16 ユーザー決定：左腕の方が完成度が高い。肩の装甲から手まで）
MIRROR_PARTS = {"LeftThigh": "RightThigh", "LeftLowerLeg": "RightLowerLeg", "LeftLeg": "RightLeg",
                "LeftShoulder": "RightShoulder", "LeftUpperArm": "RightUpperArm",
                "LeftForearm": "RightForearm", "LeftHand": "RightHand"}

# 関節の位置の手直し（Blender 座標、T ポーズにする前の元の姿勢で）。書いたボーンだけ自動の位置を上書きする。
# 自動の位置はログの「関節（自動）」に、この表へそのまま貼れる形で出る
GLB_JOINT_OVERRIDES = {
    # 肩（腕を回す中心）を上げる（2026-10-16 ユーザー：T ポーズの腕の位置が低い）。自動は z 0.351、上腕の部位の上端は 0.369
    "RightArm": (-0.0577, 0.0342, 0.368),   # 右腕は左腕の鏡写しなので左の x を反転
    "LeftArm": (0.0577, 0.0342, 0.368),
}

# 肩の装甲：鎖骨（mixamorig:{Side}Shoulder）に付けると、腕を上げたとき装甲だけ下を向いたまま残って腕が抜け出す。
# Arm と同じ位置・向きの補助のボーン {Side}ShoulderArmor（親は鎖骨）に付け、腕の回転のこの割合だけ回す。
# T ポーズで回すほか、モーションを焼き込むとき用に「回転コピー（ローカル、影響＝この割合）」を付けておく
# 組み上げたモデル（--src-blend）の関節の手直し。自動の位置はログに貼れる形で出る
PARTS_JOINT_OVERRIDES = {
}
# 組み上げたモデルはパーツを別々に生成しているので、端どうしが大きく重なっていて「接している所の中心」がずれる
# （左右で 10cm 近く食い違った）。腕・脚の途中の関節は「親と子のパーツが上下に重なっている範囲の真ん中」にし、
# 左右は平均して鏡写しにそろえ、背骨・首・頭は x = 0 にする
PARTS_MODE = False
PARTS_OVERLAP_JOINTS = {
    "ForeArm": ("UpperArm", "Forearm"),
    "Hand": ("Forearm", "Hand"),
    "Leg": ("Thigh", "LowerLeg"),
    "Foot": ("LowerLeg", "Leg"),
}
PARTS_CENTER_BONES = ("Hips", "Spine", "Spine1", "Spine2", "Neck", "Head")
# 組み上げたモデルの関節の部品の大きさの倍率（JOINT_PARTS の倍率を上書き）。ひざは 0.8 だと金の蓋が大きく横に出た（2026-10-17）
PARTS_JOINT_SCALE = {"Leg": 0.5}
JOINT_OVERRIDES = GLB_JOINT_OVERRIDES   # main で入力に合わせて替える

SHOULDER_ARMOR_FOLLOW = 0.4
ARMOR_BONE = "{side}ShoulderArmor"

# 関節の部品（ガンプラのジョイント。2026-10-16 ユーザー決定）。Tripo の部位は切り口の開いた殻なので、
# 腕・脚を回すと関節の所で切り口が見える。装甲のあいだに「中の骨組み」を見せて隠す。
# (形, 位置のボーン, ウェイトを付けるボーン, 大きさの倍率, ヒンジの軸)。位置は T ポーズ（基本の姿勢）のボーンの頭。
# 大きさは子の部位の関節の近くの太さ（ボーンの線からの距離の中央値）× 倍率。ヒンジの軸は T ポーズでの向き
#   ひじ：腕は横（±X）に伸び、前（-Y）へ曲がるので軸は Z。ひざ：脚は下（-Z）に伸び、後ろへ曲がるので軸は X
JOINT_PARTS = [
    ("ball", "Arm", "Arm", 0.95, None),
    ("ball", "UpLeg", "UpLeg", 0.65, None),
    ("hinge", "ForeArm", "Arm", 0.80, (0.0, 0.0, 1.0)),
    ("hinge", "Leg", "UpLeg", 0.80, (1.0, 0.0, 0.0)),
    ("ball", "Foot", "Leg", 0.5, None),
]
# 浮いた関節（2026-10-16 ユーザー指定。参考：モンハンワイルズのコスモロイド）：関節のまわりの球の中の装甲の面を削り、
# 球と装甲のあいだに隙間を空けて浮いて見せる。削る範囲の半径＝球の半径 × JOINT_CLEAR_SCALE。
# 削るのは関節の両側の部位だけ（位置のボーン → 削る部位のボーン）。切り口は骨組みの材質でふさぐ（裏面は描かれないので）
JOINT_CLEAR_SCALE = 1.1
JOINT_CLEAR_BONES = {
    "Arm": ["{side}Arm", "{side}ShoulderArmor"],
    "ForeArm": ["{side}Arm", "{side}ForeArm"],
    "UpLeg": ["{side}UpLeg", "Hips"],
    "Leg": ["{side}UpLeg", "{side}Leg"],
    "Foot": ["{side}Leg", "{side}Foot"],
}
JOINT_SAMPLE_LENGTH = 0.02    # 太さを測る範囲（関節から子のボーンの向きへ）[元のモデルの大きさ]
JOINT_RADIUS_RANGE = (0.008, 0.03)
# 色（2026-10-16 ユーザー決定）：テクスチャ・法線マップはやめて、マテリアルの色と質感で塗り分ける。
# 機体は白・金・青、関節は黒と金。発光は無し。(色, 金属度, 粗さ)
MECH_MATERIALS = {
    "MechWhite": ((0.86, 0.87, 0.89, 1.0), 0.1, 0.4),
    "MechGold": ((0.83, 0.62, 0.24, 1.0), 1.0, 0.3),
    "MechBlue": ((0.10, 0.28, 0.72, 1.0), 0.3, 0.35),
    "MechBlack": ((0.04, 0.045, 0.05, 1.0), 0.85, 0.35),
}
JOINT_DARK = MECH_MATERIALS["MechBlack"]
JOINT_GOLD = MECH_MATERIALS["MechGold"]
# 最初は機体を全部白にし、金と青は Blender で手で塗る。Tripo のテクスチャには白・金・青の塗り分けが無く
# （体はほぼ全体が青みの灰色の金属・彩度 0.25 前後、羽根はほぼ白）、色から振り分けると陰影のむらで迷彩のようになった
# 確認用に曲げる：(ボーン, 回す軸, 角度)。ひじを前へ・ひざを後ろへ・腕を前へ・右の股を前へ
BEND_TEST = [("RightForeArm", (0.0, 0.0, -1.0), 90.0), ("LeftForeArm", (0.0, 0.0, 1.0), 90.0),
             ("RightLeg", (1.0, 0.0, 0.0), -70.0), ("LeftLeg", (1.0, 0.0, 0.0), -70.0),
             ("RightArm", (0.0, 0.0, -1.0), 50.0), ("LeftArm", (0.0, 0.0, 1.0), 50.0),
             ("RightUpLeg", (1.0, 0.0, 0.0), 40.0)]

CONTACT_EPS = 0.004        # 部位どうしが接しているとみなす距離（関節を出すとき）
TIP_SHARE = 0.05           # 部位の先端＝関節からいちばん遠い頂点のこの割合の平均

# 羽根：列 → 関節のボーン。上の翼は 付け根(列0)→前腕(列1)→手(列2・3)、下の翼は 付け根(列0)→先(列1)
WING_ROOT = "WingRoot"
WING_CHAIN = {"Upper": ["Arm", "Fore", "Hand"], "Lower": ["Arm", "Fore"]}
WING_COLUMN_CHAIN = {"Upper": {0: 0, 1: 1, 2: 2, 3: 2}, "Lower": {0: 0, 1: 1}}
WING_FOLD_TEST_DEG = {"Arm": 25.0, "Fore": 60.0, "Hand": 50.0}   # 確認用に後ろへたたむ角度


def contact_center(points_a, points_b, eps=None):
    """a のうち b に接している点の中心。接していなければ、いちばん近い点どうしの中点"""
    eps = u(CONTACT_EPS) if eps is None else eps
    tree = make_tree(points_b[:: max(1, len(points_b) // 30000)])
    pa = points_a[:: max(1, len(points_a) // 20000)]
    found = [tree.find(p) for p in pa]
    dist = np.array([f[2] for f in found])
    near = dist < eps
    if near.sum() >= 10:
        return Vector(pa[near].mean(0))
    i = int(np.argmin(dist))
    return (Vector(pa[i]) + found[i][0]) * 0.5


def overlap_joint(parent_pts, child_pts):
    """親と子のパーツが上下（z）に重なっている範囲の真ん中。x・y は重なりの範囲にある両方の頂点の平均。
    重なっていなければ、親の下の端と子の上の端の真ん中"""
    z0 = max(parent_pts[:, 2].min(), child_pts[:, 2].min())
    z1 = min(parent_pts[:, 2].max(), child_pts[:, 2].max())
    if z1 <= z0:
        z0, z1 = sorted((parent_pts[:, 2].min(), child_pts[:, 2].max()))
    both = np.concatenate([parent_pts, child_pts])
    slab = both[(both[:, 2] >= z0) & (both[:, 2] <= z1)]
    if len(slab) < 10:
        slab = both
    return Vector((float(slab[:, 0].mean()), float(slab[:, 1].mean()), (z0 + z1) * 0.5))


def symmetrize_joints(joint):
    """左右の関節を平均して鏡写しにそろえ、真ん中の関節は x = 0 にする（左右対称に組んだモデル用）"""
    for name in list(joint):
        if name.startswith("Left"):
            other = "Right" + name[len("Left"):]
            if other in joint:
                l, r = joint[name], joint[other]
                avg = Vector(((l.x - r.x) * 0.5, (l.y + r.y) * 0.5, (l.z + r.z) * 0.5))
                joint[name] = avg
                joint[other] = Vector((-avg.x, avg.y, avg.z))
    for name in PARTS_CENTER_BONES:
        if name in joint:
            joint[name].x = 0.0


def tip_of(points, head):
    d = np.linalg.norm(points - np.array(head), axis=1)
    n = max(10, int(len(points) * TIP_SHARE))
    return Vector(points[np.argsort(-d)[:n]].mean(0))


def prepare_parts(objs):
    """親（glTF の ParentNode）を外して変形を頂点へ焼く。以後は全部の部位がワールド＝ローカル"""
    bpy.ops.object.select_all(action="DESELECT")
    for o in objs:
        o.select_set(True)
    bpy.context.view_layer.objects.active = objs[0]
    bpy.ops.object.parent_clear(type="CLEAR_KEEP_TRANSFORM")
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    for o in [x for x in bpy.data.objects if x.type == "EMPTY"]:
        bpy.data.objects.remove(o)


def new_armature(name):
    data = bpy.data.armatures.new(name)
    arm = bpy.data.objects.new(name, data)
    bpy.context.scene.collection.objects.link(arm)
    arm.show_in_front = True
    return arm


def build_bones(arm, bones):
    """bones: [(名前, head, tail, 親の名前)]"""
    bpy.ops.object.select_all(action="DESELECT")
    bpy.context.view_layer.objects.active = arm
    arm.select_set(True)
    bpy.ops.object.mode_set(mode="EDIT")
    for name, head, tail, _ in bones:
        eb = arm.data.edit_bones.new(name)
        eb.head = head
        eb.tail = tail
    for name, _, _, parent in bones:
        if parent:
            arm.data.edit_bones[name].parent = arm.data.edit_bones[parent]
    bpy.ops.object.mode_set(mode="OBJECT")


def bind_rigid(parts_to_bone, arm, mesh_name):
    """部位ごとに 1 本のボーンへウェイト 1.0 を付けて 1 つのメッシュにまとめ、アーマチュアに付ける"""
    objs = list(parts_to_bone)
    for o, bone in parts_to_bone.items():
        o.vertex_groups.clear()
        o.vertex_groups.new(name=bone).add(range(len(o.data.vertices)), 1.0, "REPLACE")
    bpy.ops.object.select_all(action="DESELECT")
    for o in objs:
        o.select_set(True)
    bpy.context.view_layer.objects.active = objs[0]
    bpy.ops.object.join()
    mesh = bpy.context.view_layer.objects.active
    mesh.name = mesh_name
    mesh.data.name = mesh_name
    mod = mesh.modifiers.new("Armature", "ARMATURE")
    mod.object = arm
    mesh.parent = arm
    return mesh


def body_bones(parts):
    co = {n: world_vertices(o) for n, o in parts.items()}
    joint = {}
    for bone, a, b in BODY_JOINTS:
        if a in co and b in co:
            joint[bone] = contact_center(co[b], co[a])
    # 組み上げたモデルは胴（Torso）と首（Neck）のパーツが無い：背骨は腰と胸のあいだ、首は胸と頭のあいだから出す
    if "Spine" not in joint:
        joint["Spine"] = contact_center(co["Chest"], co["LowerBack"])
    if "Neck" not in joint:
        joint["Neck"] = contact_center(co["Head"], co["Chest"])
    if "Head" not in joint:
        joint["Head"] = joint["Neck"] + Vector((0.0, 0.0, u(0.01)))
    if "Spine2" not in joint:
        joint["Spine2"] = joint["Spine"].lerp(joint["Neck"], 0.5)
    if PARTS_MODE:
        for side in ("Right", "Left"):
            for bone, (a, b) in PARTS_OVERLAP_JOINTS.items():
                joint[side + bone] = overlap_joint(co[side + a], co[side + b])
    # 腰：左右の股関節の真ん中（Mixamo の Hips は骨盤の中心）
    joint["Hips"] = (joint["RightUpLeg"] + joint["LeftUpLeg"]) * 0.5
    joint["Spine1"] = joint["Spine"].lerp(joint["Spine2"], 0.5)
    if PARTS_MODE:
        symmetrize_joints(joint)
    ends = {
        "Head": Vector((joint["Head"].x, joint["Head"].y, float(co["Head"][:, 2].max()))),
        "RightHand": tip_of(co["RightHand"], joint["RightHand"]),
        "LeftHand": tip_of(co["LeftHand"], joint["LeftHand"]),
    }
    for side in ("Right", "Left"):
        foot = co[side + "Leg"]
        toe = Vector(foot[np.argsort(foot[:, 1])[: max(10, len(foot) // 20)]].mean(0))   # いちばん前（-Y）
        joint[side + "ToeBase"] = toe
        ends[side + "ToeBase"] = toe + Vector((0.0, -u(0.02), 0.0))
    child_of = {}
    for bone, parent in BODY_PARENT.items():
        if parent and parent not in child_of:
            child_of[parent] = bone
    # 尾：腕と脚と首は鎖の次の関節、背骨は次の背骨
    chain_next = {
        "Hips": "Spine", "Spine": "Spine1", "Spine1": "Spine2", "Spine2": "Neck", "Neck": "Head",
        "RightShoulder": "RightArm", "RightArm": "RightForeArm", "RightForeArm": "RightHand",
        "LeftShoulder": "LeftArm", "LeftArm": "LeftForeArm", "LeftForeArm": "LeftHand",
        "RightUpLeg": "RightLeg", "RightLeg": "RightFoot", "RightFoot": "RightToeBase",
        "LeftUpLeg": "LeftLeg", "LeftLeg": "LeftFoot", "LeftFoot": "LeftToeBase",
    }
    log("関節（自動。JOINT_OVERRIDES に貼れる形）:\n" + "\n".join(
        f'    "{b}": ({p.x:.4f}, {p.y:.4f}, {p.z:.4f}),' for b, p in joint.items()))
    for bone, pos in JOINT_OVERRIDES.items():
        joint[bone] = Vector(pos)
    bones = []
    for bone, parent in BODY_PARENT.items():
        head = joint[bone]
        tail = ends[bone] if bone in ends else joint[chain_next[bone]]
        if (tail - head).length < u(1e-3):
            tail = head + Vector((0.0, 0.0, u(0.01)))
        bones.append((MX + bone, head, tail, MX + parent if parent else None))
    # 肩の装甲の補助のボーン（Arm と同じ位置・向き＝ローカルの回転コピーがそのまま効く）
    for side in ("Right", "Left"):
        arm_bone = next(b for b in bones if b[0] == MX + side + "Arm")
        bones.append((ARMOR_BONE.format(side=side), arm_bone[1], arm_bone[2], MX + side + "Shoulder"))
    return bones


def rotate_pose_bone(arm, bone_name, q):
    """ポーズでボーンを頭のまわりに q だけ回す（アーマチュアの空間）。子は一緒に回る"""
    pb = arm.pose.bones[bone_name]
    pivot = pb.head.copy()
    pb.matrix = Matrix.Translation(pivot) @ q.to_matrix().to_4x4() @ Matrix.Translation(-pivot) @ pb.matrix
    bpy.context.view_layer.update()


def pose_bone_toward(arm, bone_name, direction):
    """ポーズでボーンを direction（アーマチュアの空間）へ向ける。回した量を返す"""
    pb = arm.pose.bones[bone_name]
    q = (pb.tail - pb.head).normalized().rotation_difference(Vector(direction).normalized())
    rotate_pose_bone(arm, bone_name, q)
    return q


def mirror_parts(parts):
    """MIRROR_PARTS の左の部位を x = 0 で鏡写しにして、右の部位と置き換える"""
    for src, dst in MIRROR_PARTS.items():
        if dst in parts:
            bpy.data.objects.remove(parts[dst], do_unlink=True)
        o = parts[src]
        copy = o.copy()
        copy.data = o.data.copy()
        bpy.context.scene.collection.objects.link(copy)
        copy.data.transform(Matrix.Diagonal((-1.0, 1.0, 1.0, 1.0)))
        copy.data.flip_normals()   # 鏡写しで面の向きが裏返るので戻す
        copy.data.update()
        copy.name = dst
        copy.data.name = dst
        parts[dst] = copy
        log(f"mirror {src} -> {dst}")


def add_armor_follow_constraints(arm):
    """モーションを焼き込むとき用：肩の装甲のボーンが腕の回転の SHOULDER_ARMOR_FOLLOW だけついていく"""
    for side in ("Right", "Left"):
        pb = arm.pose.bones[ARMOR_BONE.format(side=side)]
        c = pb.constraints.new("COPY_ROTATION")
        c.target = arm
        c.subtarget = MX + side + "Arm"
        c.target_space = "LOCAL"
        c.owner_space = "LOCAL"
        c.influence = SHOULDER_ARMOR_FOLLOW


def apply_pose_as_rest(arm, mesh):
    """今のポーズをメッシュへ焼き、ボーンの基本の姿勢にする"""
    bpy.ops.object.select_all(action="DESELECT")
    bpy.context.view_layer.objects.active = mesh
    mesh.select_set(True)
    bpy.ops.object.modifier_apply(modifier="Armature")
    mod = mesh.modifiers.new("Armature", "ARMATURE")
    mod.object = arm
    bpy.ops.object.select_all(action="DESELECT")
    bpy.context.view_layer.objects.active = arm
    arm.select_set(True)
    bpy.ops.object.mode_set(mode="POSE")
    bpy.ops.pose.armature_apply(selected=False)
    bpy.ops.object.mode_set(mode="OBJECT")


def wing_info(name):
    """RightUpperWing_21 → ("Right", "Upper", 列 2, 行 1)"""
    side = "Right" if name.startswith("Right") else "Left"
    kind = "Upper" if "UpperWing" in name else "Lower"
    digits = name.split("_")[-1]
    return side, kind, int(digits[0]), int(digits[1])


def wing_bones(wings, body_parts, spine2_head):
    co = {n: world_vertices(o) for n, o in wings.items()}
    body_pts = np.concatenate([world_vertices(o) for o in body_parts.values()])
    bones = [(WING_ROOT, spine2_head, spine2_head + Vector((0.0, 0.0, 0.03)), None)]
    part_bone = {}
    for side in ("Right", "Left"):
        for kind, chain in WING_CHAIN.items():
            cols = {}
            for n in co:
                s, k, col, _ = wing_info(n)
                if s == side and k == kind:
                    cols.setdefault(WING_COLUMN_CHAIN[kind][col], []).append(n)
            group = {i: np.concatenate([co[n] for n in names]) for i, names in cols.items()}
            joints = [contact_center(group[0], body_pts)]
            for i in range(1, len(chain)):
                joints.append(contact_center(group[i], group[i - 1]))
            joints.append(tip_of(group[len(chain) - 1], joints[-1]))
            prefix = f"{side}{kind}Wing_"
            for i, link in enumerate(chain):
                parent = WING_ROOT if i == 0 else prefix + chain[i - 1]
                bones.append((prefix + link, joints[i], joints[i + 1], parent))
            # 部位ごとのボーン：列の関節から部位の先端へ（扇のように開く・羽根を 1 枚ずつ動かす用）
            for i, names in cols.items():
                for n in names:
                    bones.append((n, joints[i], tip_of(co[n], joints[i]), prefix + chain[i]))
                    part_bone[n] = n
    return bones, part_bone


def rig(objs, preview_dir):
    prepare_parts(objs)
    parts = {o.name: o for o in objs}
    mirror_parts(parts)
    body_parts = {n: o for n, o in parts.items() if n in BODY_PART_BONE}
    wings = {n: o for n, o in parts.items() if WING_MARK in n}
    unknown = [n for n in parts if n not in body_parts and n not in wings]
    if unknown:
        log(f"[注意] 体にも羽根にも当てはまらない部位（体のボーン Hips に付ける）: {unknown}")

    # ---- 体 ----
    body_arm, body, spine2_head = rig_body(parts, body_parts, unknown, preview_dir, clear_joints=True)

    # ---- 羽根（別のモデル。体と同じ原点。エンジンでは体の mixamorig:Spine2 に BoneSocket で付ける） ----
    wing_arm = new_armature("PlayerWingRig")
    wbones, part_bone = wing_bones(wings, body_parts_after(body), spine2_head)
    build_bones(wing_arm, wbones)
    wing_mesh = bind_rigid({o: part_bone[n] for n, o in wings.items()}, wing_arm, "PlayerWings")
    log(f"羽根のボーン: {len(wbones)} 本")
    recolor(body)
    recolor(wing_mesh)
    # 外したテクスチャのマテリアルと画像を消す（.blend に埋め込まれたままになるので）
    bpy.data.orphans_purge(do_local_ids=True, do_linked_ids=True, do_recursive=True)
    if preview_dir:
        preview_colors(body, wing_mesh, preview_dir)

    if preview_dir:
        preview_pose(body, wing_mesh, preview_dir, "tpose")
        preview_joints(body, wing_mesh, body_arm, preview_dir)
        fold_test(wing_arm)
        preview_pose(body, wing_mesh, preview_dir, "wings_fold_test", top=True)
        for pb in wing_arm.pose.bones:
            pb.matrix_basis = Matrix()
        bpy.context.view_layer.update()
    split_for_edit(body)
    return body_arm, wing_arm, wing_mesh


def rig_body(parts, body_parts, unknown, preview_dir, clear_joints):
    """体：Mixamo のボーン → 部位ごとにウェイト 1.0 → T ポーズを基本の姿勢に → 肩の装甲 → 関節の部品"""
    body_arm = new_armature("PlayerBodyRig")
    bones = body_bones(body_parts)
    build_bones(body_arm, bones)
    spine2_head = Vector(next(b[1] for b in bones if b[0] == MX + "Spine2"))
    body_map = {o: MX + BODY_PART_BONE[n] for n, o in body_parts.items()}
    for side in ("Right", "Left"):
        body_map[body_parts[side + "Shoulder"]] = ARMOR_BONE.format(side=side)
    for n in unknown:
        body_map[parts[n]] = MX + "Hips"
    body = bind_rigid(body_map, body_arm, "PlayerBody")
    log("体のボーン: " + ", ".join(f"{b[0]}({b[1].x:+.3f},{b[1].y:+.3f},{b[1].z:.3f})" for b in bones))

    if preview_dir:
        preview_pose(body, None, preview_dir, "body_before_tpose")
    # T ポーズ（腕は横、脚はまっすぐ下）にして、それを基本の姿勢にする
    bpy.ops.object.select_all(action="DESELECT")
    bpy.context.view_layer.objects.active = body_arm
    bpy.ops.object.mode_set(mode="POSE")
    for bone, direction in T_POSE_DIRS:
        q = pose_bone_toward(body_arm, MX + bone, direction)
        if bone.endswith("Arm") and not bone.endswith("ForeArm"):
            # 肩の装甲は腕の回転の一部だけ回す
            side = bone[: -len("Arm")]
            rotate_pose_bone(body_arm, ARMOR_BONE.format(side=side),
                             Quaternion().slerp(q, SHOULDER_ARMOR_FOLLOW))
    bpy.ops.object.mode_set(mode="OBJECT")
    apply_pose_as_rest(body_arm, body)
    add_armor_follow_constraints(body_arm)
    add_joint_parts(body, body_arm, clear=clear_joints)
    return body_arm, body, spine2_head


# ============================================================
# 組み上げたモデル（--src-blend。2026-10-17）：Tripo でパーツを 1 つずつ生成し、Blender で人の形に組んだもの。
# パーツは閉じた形なので、掃除・鏡写し・関節のまわりを削る処理は要らない。羽根は別の飛行ユニット（別のモデル）
# ============================================================
# テクスチャ（2026-10-17 ユーザー決定）：パーツのテクスチャを使う。2K に縮め、左右で同じもの（鏡写しの複製）は 1 つにする。
# 反射はパーツごとにばらばらなので、いちばん磨かれた LowerLeg（金属度・粗さのテクスチャの中央値）にそろえる。
# エンジンは金属度・粗さのテクスチャを読まず値で描くので、テクスチャはつながず値だけ入れる
PARTS_TEXTURE_SIZE = 2048
PARTS_METALLIC = 0.976
PARTS_ROUGHNESS = 0.055
PARTS_TEXTURE_DIR = "textures"   # 保存する .blend の隣。画像は .blend に埋め込まない（埋め込むと 100MB を超える）


def base_material_name(name):
    """鏡写しの複製で付いた .001 などを外す"""
    head, dot, tail = name.rpartition(".")
    return head if dot and tail.isdigit() else name


def prepare_part_materials(objs):
    """左右で同じマテリアルを 1 つにまとめ、反射をそろえ、金属度・粗さのテクスチャを外し、画像を 2K にする"""
    for o in objs:
        me = o.data
        for i, m in enumerate(me.materials):
            base = bpy.data.materials.get(base_material_name(m.name)) if m else None
            if base and base is not m:
                me.materials[i] = base
    used = {m for o in objs for m in o.data.materials if m}
    images = set()
    for m in used:
        nodes, links = m.node_tree.nodes, m.node_tree.links
        bsdf = next(n for n in nodes if n.type == "BSDF_PRINCIPLED")
        for socket in ("Metallic", "Roughness"):
            for link in list(bsdf.inputs[socket].links):
                links.remove(link)
        bsdf.inputs["Metallic"].default_value = PARTS_METALLIC
        bsdf.inputs["Roughness"].default_value = PARTS_ROUGHNESS
        # 金属度・粗さのテクスチャ（カラー分離につながっていた画像）は消す
        for sep in [n for n in nodes if n.type == "SEPARATE_COLOR"]:
            for link in sep.inputs[0].links:
                nodes.remove(link.from_node)
            nodes.remove(sep)
        for n in nodes:
            if n.type == "TEX_IMAGE" and n.image:
                images.add(n.image)
    for img in images:
        if img.size[0] > PARTS_TEXTURE_SIZE:
            img.scale(PARTS_TEXTURE_SIZE, PARTS_TEXTURE_SIZE)
    log(f"マテリアル {len(used)} 種類・画像 {len(images)} 枚（{PARTS_TEXTURE_SIZE}px、金属度 {PARTS_METALLIC}・粗さ {PARTS_ROUGHNESS}）")
    return images


def texture_file_names():
    """画像 → ファイル名（パーツ名_BaseColor / パーツ名_NormalMap）。パーツ名は左右を外したもの。
    cook は名前に NormalMap を含む画像を法線マップとして変換する"""
    names = {}
    for o in bpy.data.objects:
        if o.type != "MESH":
            continue
        part = o.name.replace("Left", "").replace("Right", "")
        for m in o.data.materials:
            if not m or not m.use_nodes:
                continue
            for link in m.node_tree.links:
                img = getattr(link.from_node, "image", None)
                if not img or img in names:
                    continue
                if link.to_socket.name == "Base Color":
                    names[img] = f"{part}_BaseColor"
                elif link.to_node.type == "NORMAL_MAP":
                    names[img] = f"{part}_NormalMap"
    return names


def write_images_beside(images, blend_path, file_names):
    """画像を .blend の隣の textures/ に PNG で書き、.blend からは相対パスで参照する。
    file_names はパーツを結合する前に texture_file_names() で取っておいたもの"""
    out_dir = blend_path.parent / PARTS_TEXTURE_DIR
    out_dir.mkdir(parents=True, exist_ok=True)
    for img in images:
        name = file_names.get(img, base_material_name(img.name))
        path = out_dir / f"{name}.png"
        img.file_format = "PNG"
        img.filepath_raw = str(path)
        img.save(filepath=str(path))
        if img.packed_file:
            img.unpack(method="REMOVE")
        img.filepath = bpy.path.relpath(str(path), start=str(blend_path.parent))
        img.reload()
    log(f"画像 {len(images)} 枚を {out_dir} へ")


def rig_parts(objs, preview_dir, save_path):
    global UNIT, PARTS_MODE
    PARTS_MODE = True
    meshes = [o for o in objs if o.type == "MESH"]
    prepare_parts(meshes)
    height = max(float(world_vertices(o)[:, 2].max()) for o in meshes)
    UNIT = height / BASE_HEIGHT
    log(f"全高 {height:.3f} → 長さの倍率 {UNIT:.3f}")
    parts = {o.name: o for o in meshes}
    body_parts = {n: o for n, o in parts.items() if n in BODY_PART_BONE}
    unknown = [n for n in parts if n not in body_parts]
    if unknown:
        log(f"[注意] 体の部位に当てはまらない（Hips に付ける）: {unknown}")
    missing = [n for n in BODY_PART_BONE if n not in parts]
    if missing:
        log(f"無い部位（そのボーンは中身なし）: {missing}")
    images = prepare_part_materials(meshes)
    file_names = texture_file_names()   # 結合するとパーツ名が分からなくなるので先に
    if preview_dir:
        # 開いた .blend のカメラ・レンダー設定ではなく、確認用のカメラと Workbench で撮る
        preview_dir.mkdir(parents=True, exist_ok=True)
        setup_render(1600, 1000)
    body_arm, body, _ = rig_body(parts, body_parts, unknown, preview_dir, clear_joints=False)
    bpy.data.orphans_purge(do_local_ids=True, do_linked_ids=True, do_recursive=True)
    if preview_dir:
        preview_pose(body, None, preview_dir, "tpose")
        preview_colors(body, None, preview_dir)
        preview_joints(body, None, body_arm, preview_dir)
    if save_path:
        save_path.parent.mkdir(parents=True, exist_ok=True)
        bpy.context.preferences.filepaths.save_version = 0
        bpy.ops.wm.save_as_mainfile(filepath=str(save_path.resolve()), compress=True)
        write_images_beside([i for i in images if i.users], save_path, file_names)
        bpy.ops.wm.save_as_mainfile(filepath=str(save_path.resolve()), compress=True)
        log(f"saved {save_path}")


# ============================================================
# 羽根（--wings。2026-10-17）：羽根は体と別の飛行ユニット（別のモデル）。前のモデルの GLB から羽根を取り出し、
# 組み上げた体（--body の PlayerModel_rig.blend）の大きさ・背中の位置に合わせて置き、ボーンを入れる。
# 大きさ：前の体の頭のてっぺん → 新しい体の頭のてっぺんの比。位置：前の体の Spine2 の高さと背中の表面を新しい体に合わせる。
# 見て直すときは WING_FIT の倍率とずれを変える。反射は元の羽根のまま（ユーザー決定）＝元の金属度・粗さのテクスチャの、
# 羽根の面の中央値を値にする（エンジンは値で描く）
# ============================================================
WING_FIT = {
    "scale": 0.85,                # 自動で出した大きさに掛ける倍率（2026-10-17 ユーザー：少し小さく）
    "offset": (0.0, 0.0, 0.0),    # 自動で出した位置からのずれ [m]（新しい体の大きさで）
}
WING_BACK_SLAB = 0.04             # 背中の表面を測る範囲（Spine2 の高さ ± これ、|x| < これ）[最初のモデルの大きさ]
# 羽根の収納・展開のアニメーション（2026-10-17 ユーザー決定）。収納した羽根は見えない（ディゾルブで消えている）。
#   Deploy：たたんだ形から開き、手首から先が少し遅れてしなり、のびきったら一度羽ばたく（展開は付け根から現れるディゾルブと合わせる）
#   Stow  ：開いた形からたたむ（収納は羽根の先から消えるディゾルブと合わせる）
# ディゾルブの濃さはエンジンが同じ秒数で動かす（アニメーションには入れない）。
# たたみ方（2026-10-17 ユーザーのスケッチ、上から見た図）：付け根からひじまでが後ろへ倒れ、ひじから先が折り返して
# 付け根の横へ戻る（U 字。ポケットナイフのように閉じる）。手首は曲げない。上の羽根も下の羽根も同じ。
# 各関節を Z 軸（上向き）まわりに回す。正の角度が後ろへ回る向き（右の翼は −Z、左は +Z）。ひじは逆向きに折り返す。
# 両翼は背中の真ん中で重ねず、左右それぞれの側で閉じる（2026-10-17 ユーザーのスケッチ）。付け根を深く倒すほど下の羽根が、
# ひじを深く折り返すほど上の羽根の先が真ん中を越えるので、越えない角度を探して決めた（上 60/−120：幅 1.03m、下 45/−115）
WING_FOLD_DEG = {
    "Upper": {"Arm": 60.0, "Fore": -120.0, "Hand": 0.0},
    "Lower": {"Arm": 45.0, "Fore": -115.0},
}
WING_ANIM_FPS = 30
# クリップ：秒数と、上の羽根（Upper）・下の羽根（Lower）の関節ごとのキー [(秒, 値)]。キーのあいだは単調な 3 次補間
# （途中のキーは速さを保って通り抜け、向きが変わるキーと両端だけ速さが 0 になる）。値の種類は
#   fold：たたみ具合（1＝WING_FOLD_DEG の角度、0＝開き切り）
#   bend：Z 軸まわりに足す角度 [度]（正＝後ろへ、負＝前へ）。手首のしなりと、羽根全体を前後に振る羽ばたき
# 展開（2026-10-17 ユーザーのスケッチ。上から見た図で下が後ろ。羽ばたきは上下ではなく前後）
#   上の羽根（6 コマ）：たたみ → V 字 → 前腕がのびて先は少し前 → 手首から先が後ろへしなる → まっすぐ → 全体が前へ振れる → 戻る
#   下の羽根（4 コマ）：たたみ → V 字 → のびて先が後ろへ → 全体が前へ振れる → 戻る
WING_CLIPS = {
    # 羽ばたきは開き切ってからではなく、展開しながら大きく（同日ユーザーのスケッチ 2・3 枚目）：
    #   開きながら、まっすぐのまま後ろへ大きく（60°）下げる → 止まらずに全体が大きく前へ振れて先が前へ反る（弓なり）
    #   → 開いた形に戻る。後ろで「ためる」とピタッと止まって見えたのでやめた（同日ユーザー指示）
    "Deploy": {
        "duration": 1.35,
        "keys": {
            "Upper": {
                "Arm": {"fold": [(0.0, 1.0), (0.55, 0.0)],
                        "bend": [(0.0, 0.0), (0.55, 60.0), (1.0, -45.0), (1.35, 0.0)]},
                "Fore": {"fold": [(0.05, 1.0), (0.55, 0.0)],
                         "bend": [(0.55, 0.0), (1.05, -20.0), (1.35, 0.0)]},
                "Hand": {"bend": [(0.6, 0.0), (1.1, -25.0), (1.35, 0.0)]},
            },
            "Lower": {
                "Arm": {"fold": [(0.0, 1.0), (0.55, 0.0)],
                        "bend": [(0.0, 0.0), (0.55, 45.0), (1.0, -35.0), (1.35, 0.0)]},
                "Fore": {"fold": [(0.05, 1.0), (0.55, 0.0)],
                         "bend": [(0.55, 0.0), (1.05, -20.0), (1.35, 0.0)]},
            },
        },
    },
    "Stow": {
        "duration": 1.0,
        "keys": {
            kind: {"Arm": {"fold": [(0.1, 0.0), (1.0, 1.0)]}, "Fore": {"fold": [(0.0, 0.0), (0.9, 1.0)]}}
            for kind in ("Upper", "Lower")
        },
    },
}


def back_surface_y(points, center_z, half):
    """背中（+Y）の表面：Spine2 の高さのまわりの、真ん中あたりの頂点のいちばん後ろ（95%）"""
    slab = points[(np.abs(points[:, 2] - center_z) < half) & (np.abs(points[:, 0]) < half)]
    return float(np.percentile(slab[:, 1], 95)) if len(slab) else float(points[:, 1].max())


def join_images(paths, out_path):
    """同じ大きさの画像を横に並べて 1 枚にし、元の画像は消す"""
    imgs = [bpy.data.images.load(str(p)) for p in paths]
    w, h = imgs[0].size
    strip = np.zeros((h, w * len(imgs), 4), dtype=np.float32)
    for i, img in enumerate(imgs):
        px = np.empty(w * h * 4, dtype=np.float32)
        img.pixels.foreach_get(px)
        strip[:, i * w:(i + 1) * w] = px.reshape(h, w, 4)
    out = bpy.data.images.new(out_path.stem, w * len(imgs), h, alpha=True)
    out.pixels.foreach_set(strip.ravel())
    out.filepath_raw = str(out_path)
    out.file_format = "PNG"
    out.save()
    for img in imgs + [out]:
        bpy.data.images.remove(img)
    for p in paths:
        Path(p).unlink()
    log(f"wrote {out_path}")


def append_objects(blend_path, names):
    with bpy.data.libraries.load(str(blend_path.resolve()), link=False) as (src, dst):
        dst.objects = [n for n in src.objects if n in names]
    for o in dst.objects:
        bpy.context.scene.collection.objects.link(o)
    return {o.name: o for o in dst.objects}


def wing_material_factors(mesh):
    """羽根の面の UV の中心で元の金属度・粗さのテクスチャを引いた中央値（金属度, 粗さ）"""
    me = mesh.data
    uv = np.empty(len(me.loops) * 2)
    me.uv_layers.active.data.foreach_get("uv", uv)
    uv = uv.reshape(-1, 2)
    starts = np.empty(len(me.polygons), dtype=np.int64)
    me.polygons.foreach_get("loop_start", starts)
    totals = np.empty(len(me.polygons), dtype=np.int64)
    me.polygons.foreach_get("loop_total", totals)
    face_uv = np.add.reduceat(uv, starts) / totals[:, None]
    for m in me.materials:
        for link in m.node_tree.links:
            if link.to_node.type == "SEPARATE_COLOR" and getattr(link.from_node, "image", None):
                img = link.from_node.image
                w, h = img.size
                px = np.empty(w * h * 4, dtype=np.float32)
                img.pixels.foreach_get(px)
                px = px.reshape(h, w, 4)
                x = np.clip((face_uv[:, 0] % 1.0) * w, 0, w - 1).astype(np.int64)
                y = np.clip((face_uv[:, 1] % 1.0) * h, 0, h - 1).astype(np.int64)
                sample = px[y, x]
                return float(np.median(sample[:, 2])), float(np.median(sample[:, 1]))
    return 0.0, 0.5


def smoothstep(x):
    x = min(max(x, 0.0), 1.0)
    return x * x * (3.0 - 2.0 * x)


def key_value(keys, t):
    """[(秒, 値)] のキーの時刻 t での値。単調な 3 次エルミート補間（Fritsch–Carlson）：途中のキーは速さを保って
    通り抜け、行き止まり（前後のキーより大きい／小さい）と両端は速さ 0。範囲の外は端の値"""
    if t <= keys[0][0]:
        return keys[0][1]
    if t >= keys[-1][0]:
        return keys[-1][1]
    ts = [k[0] for k in keys]
    vs = [k[1] for k in keys]
    n = len(keys)
    slopes = [(vs[i + 1] - vs[i]) / max(ts[i + 1] - ts[i], 1e-6) for i in range(n - 1)]
    tangents = [0.0] * n
    for i in range(1, n - 1):
        if slopes[i - 1] * slopes[i] > 0.0:
            # 調和平均：行き過ぎ（オーバーシュート）を出さない
            tangents[i] = 2.0 / (1.0 / slopes[i - 1] + 1.0 / slopes[i])
    for i in range(n - 1):
        if t <= ts[i + 1]:
            h = max(ts[i + 1] - ts[i], 1e-6)
            x = (t - ts[i]) / h
            h00 = 2 * x ** 3 - 3 * x ** 2 + 1
            h10 = x ** 3 - 2 * x ** 2 + x
            h01 = -2 * x ** 3 + 3 * x ** 2
            h11 = x ** 3 - x ** 2
            return h00 * vs[i] + h10 * h * tangents[i] + h01 * vs[i + 1] + h11 * h * tangents[i + 1]
    return vs[-1]


def wing_pose_at(clip, t):
    """クリップの時刻 t [秒] での {上/下の羽根: {関節: (fold, bend)}}"""
    pose = {}
    for kind, links in WING_CLIPS[clip]["keys"].items():
        pose[kind] = {link: tuple(key_value(ch[c], t) if c in ch else 0.0 for c in ("fold", "bend"))
                      for link, ch in links.items()}
    return pose


def pose_wing_fold(wing_arm, pose):
    """{上/下の羽根: {関節: (fold, bend)}} でポーズを付ける。付け根から順に、関節の頭のまわりに Z 軸で回す
    （たたむ・しなる・前後に振る）。右と左は鏡写しなので向きを反転する"""
    for pb in wing_arm.pose.bones:
        pb.matrix_basis = Matrix()
    bpy.context.view_layer.update()
    for side in ("Right", "Left"):
        sign = -1.0 if side == "Right" else 1.0
        for kind, links in WING_FOLD_DEG.items():
            for link in ("Arm", "Fore", "Hand"):
                if link not in links:
                    continue
                fold, bend = pose.get(kind, {}).get(link, (0.0, 0.0))
                deg = (links[link] * fold + bend) * sign
                if deg:
                    rotate_pose_bone(wing_arm, f"{side}{kind}Wing_{link}",
                                     Quaternion((0.0, 0.0, 1.0), np.radians(deg)))


def render_clip_videos(wing_arm, cam, center, out_dir):
    """クリップごとに、そのトラックだけを効かせて動画（mp4）を撮る（真上から・斜め後ろから）"""
    sc = bpy.context.scene
    saved = (sc.render.resolution_x, sc.render.resolution_y, sc.render.image_settings.file_format)
    sc.render.resolution_x, sc.render.resolution_y = 960, 600
    sc.render.image_settings.file_format = "FFMPEG"
    sc.render.ffmpeg.format = "MPEG4"
    sc.render.ffmpeg.codec = "H264"
    sc.render.fps = WING_ANIM_FPS
    tracks = wing_arm.animation_data.nla_tracks
    for clip, spec in WING_CLIPS.items():
        duration = spec["duration"]
        for track in tracks:
            track.mute = track.name != clip
        sc.frame_start, sc.frame_end = 0, int(round(duration * WING_ANIM_FPS)) + 10   # 最後に少し止める
        for view, loc in (("top", (0.0, 0.001, 3.0)), ("back", (0.0, 3.0, 0.33)), ("diag", (1.8, 2.4, 1.2))):
            look(cam, uv3(loc), center, u(1.7))
            sc.render.filepath = str(out_dir / f"clip_{clip}_{view}.mp4")
            bpy.ops.render.render(animation=True)
            log(f"wrote {sc.render.filepath}")
    for track in tracks:
        track.mute = True
    sc.render.resolution_x, sc.render.resolution_y, sc.render.image_settings.file_format = saved


def add_wing_clips(wing_arm):
    """WING_CLIPS のクリップを毎コマのキーで焼き、NLA に並べる（glTF でクリップごとに書き出せるように）"""
    bpy.ops.object.select_all(action="DESELECT")
    bpy.context.view_layer.objects.active = wing_arm
    bpy.ops.object.mode_set(mode="POSE")
    wing_arm.animation_data_create()
    chain = [pb for pb in wing_arm.pose.bones if pb.name.split("_")[-1] in ("Arm", "Fore", "Hand")]
    for pb in wing_arm.pose.bones:
        pb.rotation_mode = "QUATERNION"
    for clip, spec in WING_CLIPS.items():
        duration = spec["duration"]
        action = bpy.data.actions.new(clip)
        action.use_fake_user = True
        wing_arm.animation_data.action = action
        frames = int(round(duration * WING_ANIM_FPS))
        for f in range(frames + 1):
            pose_wing_fold(wing_arm, wing_pose_at(clip, f / WING_ANIM_FPS))
            for pb in chain:
                pb.keyframe_insert("rotation_quaternion", frame=f)
                pb.keyframe_insert("location", frame=f)
        track = wing_arm.animation_data.nla_tracks.new()
        track.name = clip
        track.strips.new(clip, 0, action)
        track.mute = True   # 焼いている間・確認画像を撮る間は、ほかのクリップがポーズを上書きしないよう止める
        wing_arm.animation_data.action = None
        log(f"羽根のクリップ {clip}: {duration} 秒・{frames + 1} コマ")
    for pb in wing_arm.pose.bones:
        pb.matrix_basis = Matrix()
    bpy.ops.object.mode_set(mode="OBJECT")
    bpy.context.scene.render.fps = WING_ANIM_FPS


def rig_wings(glb_path, body_blend, preview_dir, save_path):
    global UNIT, PARTS_MODE, JOINT_OVERRIDES, PARTS_METALLIC, PARTS_ROUGHNESS
    PARTS_MODE = True
    bpy.ops.wm.read_factory_settings(use_empty=True)
    objs = import_source(glb_path)
    fix_names(objs)
    wings = {o.name: o for o in objs if WING_MARK in o.name}
    old_body = {o.name: o for o in objs if WING_MARK not in o.name}
    clean_group(wings, lambda a, b: wing_side(a) == wing_side(b))
    prepare_parts(list(wings.values()) + list(old_body.values()))

    # 前の体：頭のてっぺん・Spine2・背中の表面
    JOINT_OVERRIDES = GLB_JOINT_OVERRIDES
    UNIT = 1.0
    old_bones = body_bones({n: o for n, o in old_body.items() if n in BODY_PART_BONE})
    old_spine2 = Vector(next(b[1] for b in old_bones if b[0] == MX + "Spine2"))
    old_top = float(world_vertices(old_body["Head"])[:, 2].max())
    old_back = back_surface_y(world_vertices(old_body["Chest"]), old_spine2.z, WING_BACK_SLAB)

    # 新しい体（確認用にも並べる。保存する前に外す）
    new = append_objects(body_blend, {"PlayerBody", "PlayerBodyRig"})
    new_body, new_rig = new["PlayerBody"], new["PlayerBodyRig"]
    new_pts = world_vertices(new_body)
    new_top = float(new_pts[:, 2].max())
    new_spine2 = new_rig.matrix_world @ new_rig.data.bones[MX + "Spine2"].head_local
    scale = new_top / old_top * WING_FIT["scale"]
    new_back = back_surface_y(new_pts, new_spine2.z, WING_BACK_SLAB * scale)
    offset = Vector((0.0, new_back - old_back * scale, new_spine2.z - old_spine2.z * scale)) + Vector(WING_FIT["offset"])
    log(f"羽根：大きさ {scale:.3f} 倍・ずらし ({offset.x:+.3f}, {offset.y:+.3f}, {offset.z:+.3f})"
        f"（前の体の頭 {old_top:.3f} → 新しい体 {new_top:.3f}、背中 {old_back:.3f} → {new_back:.3f}）")
    fit = Matrix.Translation(offset) @ Matrix.Scale(scale, 4)
    for o in list(wings.values()) + list(old_body.values()):
        o.data.transform(fit)
        o.data.update()
    UNIT = scale

    # ボーン（付け根を探すのに前の体を使い、終わったら消す）
    wing_arm = new_armature("PlayerWingRig")
    wbones, part_bone = wing_bones(wings, old_body, fit @ old_spine2)
    build_bones(wing_arm, wbones)
    wing_mesh = bind_rigid({o: part_bone[n] for n, o in wings.items()}, wing_arm, "PlayerWings")
    for o in old_body.values():
        bpy.data.objects.remove(o, do_unlink=True)
    log(f"羽根のボーン: {len(wbones)} 本")
    add_wing_clips(wing_arm)

    # 反射は元の羽根のまま。テクスチャは 2K・ファイル名は PlayerWings_BaseColor / _NormalMap
    PARTS_METALLIC, PARTS_ROUGHNESS = wing_material_factors(wing_mesh)
    images = prepare_part_materials([wing_mesh])
    file_names = {img: n for img, n in texture_file_names().items() if img in images}

    if preview_dir:
        preview_dir.mkdir(parents=True, exist_ok=True)
        cam = setup_render(1600, 1000)
        bpy.context.scene.display.shading.color_type = "TEXTURE"
        UNIT = new_top / BASE_HEIGHT
        center = uv3((0.0, 0.0, 0.33))
        views = [("wings_front", (0.0, -3.0, 0.33), 1.6), ("wings_back", (0.0, 3.0, 0.33), 1.6),
                 ("wings_side", (3.0, 0.0, 0.33), 1.6), ("wings_diag", (2.2, 2.2, 1.0), 1.6)]
        for name, loc, ortho in views:
            look(cam, uv3(loc), center, u(ortho))
            render(preview_dir / f"{name}.png")
        # クリップの途中のコマ（斜め後ろ・真上から）。コマの画像を横に並べて 1 枚にする
        for clip, spec in WING_CLIPS.items():
            duration = spec["duration"]
            for view, loc in (("diag", (1.8, 2.4, 1.2)), ("top", (0.0, 0.001, 3.0))):
                paths = []
                for i in range(6):
                    t = duration * i / 5
                    pose_wing_fold(wing_arm, wing_pose_at(clip, t))
                    look(cam, uv3(loc), center, u(1.7))
                    path = preview_dir / f"_frame_{clip}_{view}_{i}.png"
                    render(path)
                    paths.append(path)
                join_images(paths, preview_dir / f"clip_{clip}_{view}.png")
        for pb in wing_arm.pose.bones:
            pb.matrix_basis = Matrix()
        bpy.context.view_layer.update()
        render_clip_videos(wing_arm, cam, center, preview_dir)

    if wing_arm.animation_data:
        for track in wing_arm.animation_data.nla_tracks:
            track.mute = False
    # 新しい体を外して保存（画像は隣の textures/ に PNG）
    for o in (new_body, new_rig):
        bpy.data.objects.remove(o, do_unlink=True)
    for o in [o for o in bpy.data.objects if o.type == "CAMERA"]:
        bpy.data.objects.remove(o, do_unlink=True)
    bpy.data.orphans_purge(do_local_ids=True, do_linked_ids=True, do_recursive=True)
    if save_path:
        save_path.parent.mkdir(parents=True, exist_ok=True)
        bpy.context.preferences.filepaths.save_version = 0
        bpy.ops.wm.save_as_mainfile(filepath=str(save_path.resolve()), compress=True)
        write_images_beside([i for i in images if i.users], save_path, file_names)
        bpy.ops.wm.save_as_mainfile(filepath=str(save_path.resolve()), compress=True)
        log(f"saved {save_path}")


def body_parts_after(body):
    """羽根の付け根を探すための体の頂点（T ポーズの後でも胸・肩は動かないのでそのまま使える）"""
    return {"PlayerBody": body}


def fold_test(wing_arm):
    """確認用：羽根の関節を後ろ（+Y）へたたむ。右の翼は Z まわりに −、左は +"""
    bpy.ops.object.select_all(action="DESELECT")
    bpy.context.view_layer.objects.active = wing_arm
    bpy.ops.object.mode_set(mode="POSE")
    for pb in wing_arm.pose.bones:
        for link, deg in WING_FOLD_TEST_DEG.items():
            if pb.name.endswith("Wing_" + link):
                sign = -1.0 if pb.name.startswith("Right") else 1.0
                pivot = pb.head.copy()
                rot = Matrix.Rotation(np.radians(deg) * sign, 4, "Z")
                pb.matrix = Matrix.Translation(pivot) @ rot @ Matrix.Translation(-pivot) @ pb.matrix
                bpy.context.view_layer.update()
    bpy.ops.object.mode_set(mode="OBJECT")


def joint_material(name, spec):
    color, metallic, roughness = spec
    m = bpy.data.materials.get(name) or bpy.data.materials.new(name)
    m.diffuse_color = color
    m.metallic = metallic
    m.roughness = roughness
    m.use_nodes = True
    bsdf = m.node_tree.nodes.get("Principled BSDF")
    if bsdf:
        bsdf.inputs["Base Color"].default_value = color
        bsdf.inputs["Metallic"].default_value = metallic
        bsdf.inputs["Roughness"].default_value = roughness
    return m


def bone_group_vertices(mesh, bone):
    gi = mesh.vertex_groups[bone].index
    return np.array([v.co[:] for v in mesh.data.vertices
                     if any(g.group == gi and g.weight > 0.5 for g in v.groups)])


def joint_radius(mesh, arm, bone, scale):
    """子のボーンの部位の、関節の近くの太さ（ボーンの線からの距離の中央値）× 倍率"""
    b = arm.data.bones[bone]
    head = np.array(b.head_local)
    axis = np.array((b.tail_local - b.head_local).normalized())
    pts = bone_group_vertices(mesh, bone) - head
    t = pts @ axis
    near = (t > -u(0.002)) & (t < u(JOINT_SAMPLE_LENGTH))
    if near.sum() < 20:
        near = t < u(JOINT_SAMPLE_LENGTH) * 2
    radial = np.linalg.norm(pts[near] - np.outer(t[near], axis), axis=1)
    return float(np.clip(np.median(radial) * scale, u(JOINT_RADIUS_RANGE[0]), u(JOINT_RADIUS_RANGE[1])))


def axis_matrix(center, axis):
    """原点 center・Z 軸を axis に向ける行列"""
    rot = Vector((0.0, 0.0, 1.0)).rotation_difference(Vector(axis).normalized()).to_matrix().to_4x4()
    return Matrix.Translation(center) @ rot


def add_cylinder(bm, center, axis, radius, length, segments=24):
    """axis 向きの円柱（両端に蓋）を bm に足し、足した面を返す"""
    ret = bmesh.ops.create_cone(bm, cap_ends=True, cap_tris=False, segments=segments,
                                radius1=radius, radius2=radius, depth=length, matrix=axis_matrix(center, axis))
    return {f for v in ret["verts"] for f in v.link_faces}


def build_joint(kind, center, axis, radius):
    """関節の部品 1 つ（材質 0＝骨組み、1＝金）"""
    bm = bmesh.new()
    gold = set()
    if kind == "ball":
        bmesh.ops.create_uvsphere(bm, u_segments=24, v_segments=16, radius=radius,
                                  matrix=Matrix.Translation(center))
        # 骨の向きに直交する金の輪（球より少し太い短い帯）
        gold |= add_cylinder(bm, center, axis, radius * 1.04, radius * 0.32)
    else:
        add_cylinder(bm, center, axis, radius, radius * 1.7)
        # 軸の両端に金の蓋（少し太い薄い円盤）
        a = Vector(axis).normalized()
        for sign in (-1.0, 1.0):
            gold |= add_cylinder(bm, center + a * (sign * radius * 0.9), a, radius * 1.12, radius * 0.22)
    for f in bm.faces:
        f.material_index = 1 if f in gold else 0
        f.smooth = True
    return bm


def bone_name(template, side):
    name = template.format(side=side)
    return name if name.startswith(side + "ShoulderArmor") else MX + name


def clear_around_joints(body, clears):
    """clears: [(中心, 半径, 削る部位のボーン名)]。球の中の面を削る（できた穴は編集用の .blend で手で埋める）"""
    me = body.data
    group_index = {g.name: g.index for g in body.vertex_groups}
    co = np.empty(len(me.vertices) * 3)
    me.vertices.foreach_get("co", co)
    co = co.reshape(-1, 3)
    # 体の頂点はどれも 1 つのボーンにウェイト 1.0（リジッド）なので、最初のグループがその部位
    vgroup = np.array([v.groups[0].group if len(v.groups) else -1 for v in me.vertices])
    doomed_verts = np.zeros(len(co), dtype=bool)
    for center, radius, bones in clears:
        d = np.linalg.norm(co - np.array(center), axis=1)
        groups = [group_index[b] for b in bones if b in group_index]
        doomed_verts |= (d < radius) & np.isin(vgroup, groups)
    loops = np.empty(len(me.loops), dtype=np.int64)
    me.loops.foreach_get("vertex_index", loops)
    starts = np.empty(len(me.polygons), dtype=np.int64)
    me.polygons.foreach_get("loop_start", starts)
    doomed_faces = np.flatnonzero(np.logical_or.reduceat(doomed_verts[loops], starts))

    bm = bmesh.new()
    bm.from_mesh(me)
    bm.faces.ensure_lookup_table()
    bmesh.ops.delete(bm, geom=[bm.faces[i] for i in doomed_faces], context="FACES")
    bm.to_mesh(me)
    bm.free()
    me.update()
    log(f"関節のまわりを削った面 {len(doomed_faces)}")


def add_joint_parts(body, arm, clear=True):
    """JOINT_PARTS の部品を作って体のメッシュに足す（部品ごとにウェイト 1.0）。
    clear なら先に関節のまわりの装甲を削って、球と装甲のあいだに隙間を空ける（浮いた関節。最初のモデル用）"""
    dark = joint_material("MechBlack", JOINT_DARK)
    gold = joint_material("MechGold", JOINT_GOLD)
    plan = []
    for kind, at_bone, weight_bone, scale, hinge_axis in JOINT_PARTS:
        if PARTS_MODE:
            scale = PARTS_JOINT_SCALE.get(at_bone, scale)
        for side in ("Right", "Left"):
            radius = joint_radius(body, arm, MX + side + at_bone, scale)
            plan.append((kind, at_bone, weight_bone, hinge_axis, side, radius))
    clears = []
    for kind, at_bone, _, _, side, radius in plan:
        center = arm.data.bones[MX + side + at_bone].head_local.copy()
        clears.append((center, radius * JOINT_CLEAR_SCALE,
                       [bone_name(t, side) for t in JOINT_CLEAR_BONES[at_bone]]))
    if clear:
        clear_around_joints(body, clears)
    made = []
    for kind, at_bone, weight_bone, hinge_axis, side, radius in plan:
        b = arm.data.bones[MX + side + at_bone]
        center = b.head_local.copy()
        axis = hinge_axis or (b.tail_local - b.head_local).normalized()
        bm = build_joint(kind, center, axis, radius)
        me = bpy.data.meshes.new(f"Joint_{side}{at_bone}")
        bm.to_mesh(me)
        bm.free()
        me.materials.append(dark)
        me.materials.append(gold)
        o = bpy.data.objects.new(me.name, me)
        bpy.context.scene.collection.objects.link(o)
        o.vertex_groups.new(name=MX + side + weight_bone).add(range(len(me.vertices)), 1.0, "REPLACE")
        made.append(o)
        log(f"関節の部品 {me.name}: {kind} 半径 {radius:.4f} -> {MX + side + weight_bone}")
    bpy.ops.object.select_all(action="DESELECT")
    for o in made:
        o.select_set(True)
    body.select_set(True)
    bpy.context.view_layer.objects.active = body
    bpy.ops.object.join()


def recolor(obj):
    """テクスチャのマテリアルを外して機体を白（MechWhite）にする。関節の部品（MechBlack / MechGold）はそのまま。
    マテリアルの並び：0 白・1 金・2 青・3 黒（金は機体と関節で同じマテリアル）"""
    me = obj.data
    keep = {i: m.name for i, m in enumerate(me.materials) if m and m.name in ("MechBlack", "MechGold")}
    face_mat = np.empty(len(me.polygons), dtype=np.int64)
    me.polygons.foreach_get("material_index", face_mat)
    new_index = np.zeros(len(me.polygons), dtype=np.int64)
    for i, name in keep.items():
        new_index[face_mat == i] = 3 if name == "MechBlack" else 1
    me.materials.clear()
    for name in ("MechWhite", "MechGold", "MechBlue", "MechBlack"):
        me.materials.append(joint_material(name, MECH_MATERIALS[name]))
    me.polygons.foreach_set("material_index", new_index)
    me.update()
    counts = np.bincount(new_index, minlength=4)
    log(f"{obj.name} の色: 白 {counts[0]}・金 {counts[1]}・青 {counts[2]}・黒 {counts[3]} 面")


# ============================================================
# 編集用の .blend（2026-10-16 ユーザー指定）：体を腕・脚・胴に分け、関節の部品も込みで手で穴を埋められるようにする。
# 直したら --finish でまとめ直す
# ============================================================
EDIT_CHAINS = {
    "Body_RightArm": ["mixamorig:RightShoulder", "RightShoulderArmor", "mixamorig:RightArm",
                      "mixamorig:RightForeArm", "mixamorig:RightHand"],
    "Body_LeftArm": ["mixamorig:LeftShoulder", "LeftShoulderArmor", "mixamorig:LeftArm",
                     "mixamorig:LeftForeArm", "mixamorig:LeftHand"],
    "Body_RightLeg": ["mixamorig:RightUpLeg", "mixamorig:RightLeg", "mixamorig:RightFoot", "mixamorig:RightToeBase"],
    "Body_LeftLeg": ["mixamorig:LeftUpLeg", "mixamorig:LeftLeg", "mixamorig:LeftFoot", "mixamorig:LeftToeBase"],
}
EDIT_REST = "Body_Torso"   # 上のどれにも入らない部位（腰・背骨・首・頭）


def split_for_edit(body):
    """体を EDIT_CHAINS ごとのオブジェクトに分ける（頂点グループ・アーマチュアはそのまま。ポーズで曲げて確かめられる）"""
    group_names = {g.index: g.name for g in body.vertex_groups}
    chain_of = {}
    for chain, bones in EDIT_CHAINS.items():
        for b in bones:
            chain_of[b] = chain
    owner = np.array([chain_of.get(group_names.get(v.groups[0].group if len(v.groups) else -1), EDIT_REST)
                      for v in body.data.vertices])
    made = []
    for chain in list(EDIT_CHAINS) + [EDIT_REST]:
        o = body.copy()
        o.data = body.data.copy()
        o.name = chain
        o.data.name = chain
        bpy.context.scene.collection.objects.link(o)
        bm = bmesh.new()
        bm.from_mesh(o.data)
        bm.verts.ensure_lookup_table()
        bmesh.ops.delete(bm, geom=[bm.verts[i] for i in np.flatnonzero(owner != chain)], context="VERTS")
        bm.to_mesh(o.data)
        bm.free()
        made.append(o)
        log(f"編集用 {chain}: {len(o.data.vertices)} 頂点")
    bpy.data.objects.remove(body, do_unlink=True)
    return made


def finish_edit():
    """手で直した Body_* を PlayerBody にまとめ直す。ウェイトの無い頂点（手で足した所）はいちばん近い頂点の部位に付ける"""
    parts = [o for o in bpy.data.objects if o.type == "MESH" and o.name.startswith("Body_")]
    if not parts:
        raise RuntimeError("Body_* のオブジェクトが無い（編集用の .blend を渡すこと）")
    arm = bpy.data.objects["PlayerBodyRig"]
    bpy.ops.object.select_all(action="DESELECT")
    for o in parts:
        o.select_set(True)
    bpy.context.view_layer.objects.active = parts[0]
    bpy.ops.object.join()
    body = bpy.context.view_layer.objects.active
    body.name = "PlayerBody"
    body.data.name = "PlayerBody"
    me = body.data
    weighted = [v.index for v in me.vertices if len(v.groups)]
    missing = [v.index for v in me.vertices if not len(v.groups)]
    if missing:
        co = np.array([v.co[:] for v in me.vertices])
        tree = make_tree(co[weighted])
        for i in missing:
            src = me.vertices[weighted[tree.find(co[i])[1]]]
            g = src.groups[0]
            body.vertex_groups[g.group].add([i], 1.0, "REPLACE")
    if not any(m.type == "ARMATURE" for m in body.modifiers):
        body.modifiers.new("Armature", "ARMATURE").object = arm
    body.parent = arm
    log(f"まとめ直した: {len(parts)} 個 → PlayerBody（ウェイトを付けた頂点 {len(missing)}）")


def mirror_side(name):
    return name.replace("Left", "\0").replace("Right", "Left").replace("\0", "Right")


def mirror_chain_in_edit(left_obj_name, right_obj_name, bone_names):
    """編集用の .blend の中で、右の Body_* を左の Body_* の鏡写しに置き換え、右のボーンも左の鏡写しの位置にする。
    ほかのオブジェクト（手で直した所）はそのまま。bone_names は左のボーン名"""
    arm = bpy.data.objects["PlayerBodyRig"]
    src = bpy.data.objects[left_obj_name]
    old = bpy.data.objects.get(right_obj_name)
    if old:
        bpy.data.objects.remove(old, do_unlink=True)
    copy = src.copy()
    copy.data = src.data.copy()
    bpy.context.scene.collection.objects.link(copy)
    copy.data.transform(Matrix.Diagonal((-1.0, 1.0, 1.0, 1.0)))
    copy.data.flip_normals()
    copy.data.update()
    copy.name = right_obj_name
    copy.data.name = right_obj_name
    # 頂点グループの名前を左 → 右に（Left と Right を入れ替える）
    for g in copy.vertex_groups:
        g.name = "__tmp__" + mirror_side(g.name)
    for g in copy.vertex_groups:
        g.name = g.name[len("__tmp__"):]
    # ボーン：右を左の鏡写しの位置に（ロールも反転）
    bpy.ops.object.select_all(action="DESELECT")
    bpy.context.view_layer.objects.active = arm
    arm.select_set(True)
    bpy.ops.object.mode_set(mode="EDIT")
    eb = arm.data.edit_bones
    for name in bone_names:
        left, right = eb[name], eb[mirror_side(name)]
        right.head = (-left.head.x, left.head.y, left.head.z)
        right.tail = (-left.tail.x, left.tail.y, left.tail.z)
        right.roll = -left.roll
    bpy.ops.object.mode_set(mode="OBJECT")
    log(f"{right_obj_name} を {left_obj_name} の鏡写しに置き換えた（ボーン {len(bone_names)} 本も鏡写し）")


def bend_test(arm, on):
    """確認用：ひじ・ひざ・肩・股を BEND_TEST だけ曲げる（on=False で戻す）"""
    bpy.ops.object.select_all(action="DESELECT")
    bpy.context.view_layer.objects.active = arm
    bpy.ops.object.mode_set(mode="POSE")
    if on:
        for bone, axis, deg in BEND_TEST:
            rotate_pose_bone(arm, MX + bone, Quaternion(Vector(axis), np.radians(deg)))
    else:
        for pb in arm.pose.bones:
            pb.matrix_basis = Matrix()
        bpy.context.view_layer.update()
    bpy.ops.object.mode_set(mode="OBJECT")


def preview_joints(body, wings, arm, out_dir):
    """関節の部品の寄り（材質の色で塗る。羽根は隠す）と、曲げた姿勢"""
    sc = bpy.context.scene
    cam = sc.camera or setup_render(1600, 1000)
    sc.display.shading.color_type = "MATERIAL"
    if wings:
        wings.hide_render = True
    views = [
        ("joint_shoulder_front", (-0.06, -1.0, 0.36), (-0.06, 0.03, 0.36), 0.2),
        ("joint_shoulder_back", (-0.06, 1.0, 0.36), (-0.06, 0.03, 0.36), 0.2),
        ("joint_legs_front", (0.0, -1.0, 0.17), (0.0, 0.02, 0.17), 0.3),
        ("joint_legs_side", (1.0, 0.02, 0.17), (0.0, 0.02, 0.17), 0.3),
        ("body_back_no_wings", (0.0, 1.0, 0.30), (0.0, 0.02, 0.30), 0.5),
        ("body_upper_back_no_wings", (0.0, 1.0, 0.37), (0.0, 0.02, 0.37), 0.22),
    ]
    for name, loc, target, ortho in views:
        look(cam, uv3(loc), uv3(target), u(ortho))
        render(out_dir / f"{name}.png")
    bend_test(arm, True)
    look(cam, uv3((0.7, -0.9, 0.35)), uv3((0.0, 0.0, 0.26)), u(0.55))
    render(out_dir / "joint_bend_test.png")
    look(cam, uv3((-0.06, -1.0, 0.33)), uv3((-0.06, 0.02, 0.33)), u(0.22))
    render(out_dir / "joint_bend_test_arm.png")
    bend_test(arm, False)
    if wings:
        wings.hide_render = False
    sc.display.shading.color_type = "OBJECT"


def preview_colors(body, wings, out_dir):
    """マテリアルの色（白・金・青・黒）で正面・背面・斜め。組み上げたモデルはテクスチャで"""
    sc = bpy.context.scene
    cam = sc.camera or setup_render(1600, 1000)
    sc.display.shading.color_type = "TEXTURE" if PARTS_MODE else "MATERIAL"
    center = uv3((0.0, 0.0, 0.28))
    for name, loc in (("colors_front", (0.0, -3.0, 0.28)), ("colors_back", (0.0, 3.0, 0.28)),
                      ("colors_diag", (-2.0, -2.2, 0.9))):
        look(cam, uv3(loc), center, u(1.1))
        render(out_dir / f"{name}.png")
    sc.display.shading.color_type = "OBJECT"


def preview_pose(body, wings, out_dir, label, top=False):
    out_dir.mkdir(parents=True, exist_ok=True)
    cam = bpy.context.scene.camera or setup_render(1600, 1000)
    sc = bpy.context.scene
    sc.display.shading.color_type = "OBJECT"
    body.color = (0.75, 0.78, 0.85, 1.0)
    if wings:
        wings.color = (0.95, 0.85, 0.45, 1.0)
    center = uv3((0.0, 0.0, 0.28))
    look(cam, uv3((0.0, -3.0, 0.28)), center, u(1.1))
    render(out_dir / f"{label}_front.png")
    look(cam, uv3((3.0, 0.0, 0.28)), center, u(1.1))
    render(out_dir / f"{label}_side.png")
    if top:
        look(cam, uv3((0.0, 0.001, 3.0)), center, u(1.1))
        render(out_dir / f"{label}_top.png")


# ============================================================
# 体のクリップ（--body-clips。2026-10-18 ユーザー決定：スタート演出に要る分だけ先に作る。Mixamo のリターゲットは後）
#   Catapult：カタパルトで射出を待つ・射出するときの、膝を曲げた前傾。羽根は収納（見えない）
#   Fly     ：射出の後、雲の上を飛ぶ姿勢。羽根は展開（羽根は別のモデルで、体の Spine2 に付ける）
# 角度は右側で書き、左は x = 0 の鏡写しにする。向きはアーマチュアの空間（正面は -Y、+X がモデルの左、+Z が上）。
# カタパルトではエンジンが体を HubMech と同じ 18m に拡大し、両足を固定台 (±3.0, 0, 1.2) に乗せる
# → 1.694m の体では x ±0.282・前へ 0.113。足首の位置を IK で決め、足の裏は基本の姿勢のまま床に平らに置く
# ============================================================
BODY_ANIM_FPS = WING_ANIM_FPS
CATAPULT_SCALE_HEIGHT = 18.0          # カタパルトでの全高 [m]（HubMech と同じ）
CATAPULT_CLAMP = (3.0, 1.2)           # 固定台の中心 (|x|, 前) [m]（16 番 §2.1 の CatapultClamp）。足の中心をここへ
BODY_HEIGHT = [1.694]                 # 体の全高 [m]（--body-clips で測り直す）
# 姿勢。値の意味：
#   hips  … 腰のずらし [m] と前傾 [度]      bend … X 軸まわり [度]（正＝前へ倒す、負＝起こす）
#   arms  … 腕のボーンを向ける向き           legs … ik：足首を固定台の上に置く（knee＝ひざを出す側、toe_out＝つま先の開き [度]、
#            heel_lift＝つま先を支点にかかとを上げる [度]）／ik でなければボーンを向ける向き
#   ankle_lean … 足首を支点に、上体の形はそのまま体全体を前へ倒す [度]（2026-10-18 ユーザー）
#   tilt  … 最後に腰のまわりで脚ごと前へ倒す [度]   sway … ループの小さな動き
BODY_POSES = {
    # 射出前（カタパルトで機体確認をしている間）：直立・動かない（2026-10-18 ユーザー）。足は固定台の上なので
    # 肩幅より広く、腰を少し下げて脚をわずかにハの字に。腕は体の横へ下ろす
    "Stand": {
        # 足首は足の中心を固定台に合わせて前へ 6.7cm ずれ、基本の姿勢の背骨も上ほど後ろへ傾いているので、そのままだと
        # 後ろへ反って見えた（2026-10-18 ユーザー）。足首から前へ倒し、腰の背骨を少し曲げて頭を足の真上へ
        "hips": {"offset": (0.0, 0.0, -0.04), "lean": 0.0},
        "bend": {"Spine": 3.0, "Spine1": 3.0},
        "arms": {"Arm": (-0.12, 0.0, -1.0), "ForeArm": (-0.08, -0.12, -1.0)},
        "legs": {"ik": True, "knee": (-0.1, -1.0, 0.0), "toe_out": 0.0},
        "ankle_lean": 4.0,
    },
    # 射出が始まってから（エンジンを吹かす）の前傾。ロボットなので呼吸の動きは付けない（2026-10-18 ユーザー）
    "Catapult": {
        "hips": {"offset": (0.0, 0.06, -0.22), "lean": 12.0},
        "bend": {"Spine": 8.0, "Spine1": 8.0, "Spine2": 8.0, "Neck": -10.0, "Head": -18.0},
        "arms": {"Arm": (-0.35, 0.35, -1.0), "ForeArm": (-0.1, -0.5, -1.0)},
        # つま先はレールに平行（開くと固定台の爪からはみ出す。2026-10-18）
        "legs": {"ik": True, "knee": (-0.35, -1.0, 0.0), "toe_out": 0.0},
        "ankle_lean": 8.0,
    },
    # 切り離しの溜め：もう少し沈み、腕を後ろへ引く
    "LaunchDip": {
        "hips": {"offset": (0.0, 0.08, -0.29), "lean": 16.0},
        "bend": {"Spine": 8.0, "Spine1": 8.0, "Spine2": 8.0, "Neck": -14.0, "Head": -20.0},
        "arms": {"Arm": (-0.3, 0.6, -1.0), "ForeArm": (-0.1, 0.0, -1.0)},
        "legs": {"ik": True, "knee": (-0.35, -1.0, 0.0), "toe_out": 0.0},
        "ankle_lean": 10.0,
    },
    # けり出し：かかとを上げてつま先で床をけり、脚を伸ばして体全体が前へ倒れる。腕は後ろへ振る
    "LaunchPush": {
        "hips": {"offset": (0.0, 0.0, 0.05), "lean": 10.0},
        "bend": {"Spine": 4.0, "Spine1": 4.0, "Spine2": 4.0, "Neck": -18.0, "Head": -22.0},
        "arms": {"Arm": (-0.25, 0.9, -0.5), "ForeArm": (-0.2, 0.8, -0.6)},
        "legs": {"ik": True, "knee": (-0.2, -1.0, 0.0), "toe_out": 0.0, "heel_lift": 35.0},
        "ankle_lean": 30.0,
    },
    "Fly": {
        # 体全体を前へ傾ける（2026-10-18 ユーザー：上から吊られているように見える）
        "tilt": 40.0,
        "hips": {"offset": (0.0, 0.0, 0.0), "lean": 10.0},
        "bend": {"Spine": 3.0, "Spine1": 3.0, "Spine2": 3.0, "Neck": -20.0, "Head": -25.0},
        "arms": {"Arm": (-0.45, 0.3, -1.0), "ForeArm": (-0.25, -0.1, -1.0)},
        "legs": {"ik": False,
                 "Right": {"UpLeg": (-0.03, 0.15, -1.0), "Leg": (0.0, 0.35, -1.0), "Foot": (0.0, 0.25, -1.0)},
                 "Left": {"UpLeg": (0.03, 0.30, -1.0), "Leg": (0.0, 0.95, -0.8), "Foot": (0.0, 0.8, -1.0)}},
        "sway": {"spine": 1.0, "hips_z": 0.012, "arm": 3.0, "leg": 3.0},
    },
}
# クリップ（エンジンでは PlayerModel_<名前>.anim）。pose＝その姿勢で sway のループ。
# keys＝[(秒, 姿勢)] のあいだをつなぐ 1 回きりのクリップ（ボーンごとに位置を線形・回転を球面線形、キーのあいだは smoothstep）
BODY_CLIPS = {
    "Stand": {"duration": 1.0, "pose": "Stand"},                  # 機体確認の間の直立（ループ・動かない）
    "Ignite": {"duration": 0.5, "keys": [(0.0, "Stand"), (0.5, "Catapult")]},   # 直立 → 前傾（1 回）
    "Catapult": {"duration": 1.0, "pose": "Catapult"},            # 前傾の保持（ループ・動かない）
    "Launch": {"duration": 0.9, "keys": [(0.0, "Catapult"), (0.15, "LaunchDip"),
                                        (0.45, "LaunchPush"), (0.9, "Fly")]},   # 切り離しのジャンプ（1 回）
    "Fly": {"duration": 2.0, "pose": "Fly"},                      # 雲の上（ループ）
    # HUB の乗り込み（2026-10-18 ユーザー）：「搭乗」で直立から右手を胸の前へ差し出し、パイロットが手のひらを伝って胸へ入る
    "Offer": {"duration": 1.6, "keys": [(0.0, "Stand"), (1.6, "Offer")]},       # 直立 → 手を差し出す（1 回）
    "OfferHold": {"duration": 1.0, "pose": "Offer"},              # 差し出したまま（ループ・動かない。降りるの始まり）
    "Retract": {"duration": 1.4, "keys": [(0.0, "Offer"), (1.4, "Stand")]},     # 手を戻して直立へ（1 回）
}

# HUB で右手を差し出す姿勢（18_HubTransitions.md ①）。値はエンジンの座標 [m]（機体の原点から、正面 +Z。
# HUB の機体の大きさはカタパルトと同じ 18m）。Blender のアーマチュアの空間へは (x, y, z) → (−x, −z, y) / 拡大率
HUB_OFFER = {
    "palm": (0.0, 11.0, 3.0),        # 手のひらの上面の中心。足場（上面 10.6・先 z 3.85、gen_hub_temple.py）の先で 0.4m 上（ユーザー：もう少し上）
    "fingers": (-1.0, 0.0, 0.3),     # 指先の向き。体の内側（右手は +X 側なので −X）へ、少し前へ
    "elbow": (1.0, -0.6, 0.2),       # ひじを出す側（外・下）
    "clavicle": 0.0,                 # 鎖骨（Shoulder）を前へ回す [度]。届かないときに足す
}
# 基本の姿勢（T ポーズ）の手のひらの向き（アーマチュアの空間）。手のひらは下（下から撮って確かめた。頂点の薄い向きは
# 親指に引かれて 27° 傾くので使わない）
HAND_REST_PALM_NORMAL = (0.0, 0.0, -1.0)
BODY_POSES["Offer"] = {**BODY_POSES["Stand"], "arm_ik": HUB_OFFER}


def side_vec(v, side):
    """右側で書いた向きを side の向きに（左は x を反転）"""
    return Vector((v[0] if side == "Right" else -v[0], v[1], v[2]))


def catapult_scale():
    return CATAPULT_SCALE_HEIGHT / BODY_HEIGHT[0]


def body_ankle_target(arm, side, heel_lift=0.0):
    """カタパルトの固定台の上の足首の位置：足の中心が固定台の中心に来て、足の裏が床に着く所。
    heel_lift のときは足の指の付け根（つま先のいちばん低い点）を支点にかかとを上げた所"""
    fit = FOOT_FIT[side]
    rest = arm.data.bones[MX + side + "Foot"].head_local
    x = CATAPULT_CLAMP[0] / catapult_scale()
    ankle = Vector((-x if side == "Right" else x, -CATAPULT_CLAMP[1] / catapult_scale() - fit["center_y"],
                    rest.z + fit["drop"]))
    if heel_lift:
        ball = Vector(fit["ball"])
        ankle = ankle + ball - Matrix.Rotation(np.radians(heel_lift), 3, "X") @ ball
    return ankle


def leg_ik(arm, side, ankle, knee_hint):
    """2 本のボーン（UpLeg・Leg）の IK：足首を ankle に置き、ひざを knee_hint の側へ出す（届かなければ伸び切る）"""
    up = arm.pose.bones[MX + side + "UpLeg"]
    leg = arm.pose.bones[MX + side + "Leg"]
    hip = up.head.copy()
    l1, l2 = up.bone.length, leg.bone.length
    axis = ankle - hip
    d = min(axis.length, (l1 + l2) * 0.999)
    axis.normalize()
    a = (l1 * l1 - l2 * l2 + d * d) / (2.0 * d)
    h = np.sqrt(max(l1 * l1 - a * a, 0.0))
    hint = side_vec(knee_hint, side)
    perp = (hint - axis * hint.dot(axis)).normalized()
    knee = hip + axis * a + perp * h
    pose_bone_toward(arm, up.name, knee - hip)
    pose_bone_toward(arm, leg.name, hip + axis * d - knee)


# 足の裏を床に着ける（2026-10-18 ユーザー：つま先が浮いている）。T ポーズの足はつま先上がり（先端で 9cm）なので、
# 足首のまわりに前へ倒して足の裏の前と後ろの高さをそろえ、いちばん低い所が床に来るよう足首を下げる。
# {side: {pitch：前へ倒す角度 [ラジアン]、drop：足首を上下にずらす量 [m]、center_y：倒した足の前後の中心（足首から）、
#         ball：倒した足のつま先のいちばん低い点（足首から）}}。--body-clips でメッシュから測る
FOOT_FIT = {side: {"pitch": 0.0, "drop": 0.0, "center_y": 0.0, "ball": (0.0, 0.0, 0.0)} for side in ("Right", "Left")}
FOOT_SOLE_RANGE = (0.11, 0.06)   # 足の裏の後ろ（かかと）・前（つま先）として見る前後の幅 [m]


def fit_feet(body, arm):
    """足の部位の頂点（基本の姿勢）から FOOT_FIT を測る：かかと・つま先それぞれのいちばん低い点が同じ高さになる角度と、
    倒した後のいちばん低い点を床（体のいちばん低い点）に合わせる足首のずらし"""
    me = body.data
    co = np.empty(len(me.vertices) * 3)
    me.vertices.foreach_get("co", co)
    co = co.reshape(-1, 3)
    ground = float(co[:, 2].min())
    scale = catapult_scale()
    for side in ("Right", "Left"):
        group = body.vertex_groups[MX + side + "Foot"].index
        idx = [v.index for v in me.vertices if any(g.group == group and g.weight > 0.5 for g in v.groups)]
        pts = co[idx] - np.array(arm.data.bones[MX + side + "Foot"].head_local)
        back = pts[pts[:, 1] > pts[:, 1].max() - FOOT_SOLE_RANGE[0]]   # かかとの側（+Y が後ろ）
        front = pts[pts[:, 1] < pts[:, 1].min() + FOOT_SOLE_RANGE[1]]
        heel = back[np.argmin(back[:, 2])]
        toe = front[np.argmin(front[:, 2])]
        # X 軸まわりに a 回すと y' = y cos a − z sin a、z' = y sin a + z cos a。かかとと つま先の z' が等しくなる a
        dy, dz = toe[1] - heel[1], toe[2] - heel[2]
        pitch = float(np.arctan2(dz, -dy))
        c, s = np.cos(pitch), np.sin(pitch)
        ry = pts[:, 1] * c - pts[:, 2] * s
        rz = pts[:, 1] * s + pts[:, 2] * c
        head_z = arm.data.bones[MX + side + "Foot"].head_local.z
        drop = ground - (head_z + float(rz.min()))
        center_y = float(ry.min() + ry.max()) * 0.5
        ball = (float(toe[0]), float(toe[1] * c - toe[2] * s), float(toe[1] * s + toe[2] * c))
        FOOT_FIT[side] = {"pitch": pitch, "drop": drop, "center_y": center_y, "ball": ball}
        log(f"{side} の足：前へ {np.degrees(pitch):.1f}° 倒し、足首を {drop * 100:+.1f}cm。カタパルトでの足の前後 "
            f"{(-ry.max() + center_y) * scale + CATAPULT_CLAMP[1]:+.2f}〜{(-ry.min() + center_y) * scale + CATAPULT_CLAMP[1]:+.2f} m・"
            f"幅 {float(np.ptp(pts[:, 0])) * scale:.2f} m（エンジンの z・x）")


def foot_flat(arm, side, toe_out, heel_lift=0.0):
    """足の裏を床に平らに置き（FOOT_FIT）、つま先を外へ toe_out 度開き、かかとを heel_lift 度上げる"""
    pb = arm.pose.bones[MX + side + "Foot"]
    yaw = np.radians(toe_out) * (-1.0 if side == "Right" else 1.0)
    rest = pb.bone.matrix_local.to_3x3()
    pitch = FOOT_FIT[side]["pitch"] + np.radians(heel_lift)
    rot = Matrix.Rotation(yaw, 3, "Z") @ Matrix.Rotation(pitch, 3, "X") @ rest
    pb.matrix = Matrix.Translation(pb.head.copy()) @ rot.to_4x4()
    bpy.context.view_layer.update()


def pose_body(arm, name, phase=0.0):
    """BODY_POSES の姿勢 name を付ける（基本の姿勢から、親から順に）。phase はループの位置（0〜1、sway に使う）"""
    spec = BODY_POSES[name]
    wave = np.sin(2.0 * np.pi * phase)
    sway = spec.get("sway", {})
    for pb in arm.pose.bones:
        pb.matrix_basis = Matrix()
    bpy.context.view_layer.update()
    x_axis = Vector((1.0, 0.0, 0.0))

    hips = arm.pose.bones[MX + "Hips"]
    offset = Vector(spec["hips"]["offset"]) + Vector((0.0, 0.0, sway.get("hips_z", 0.0) * wave))
    hips.matrix = Matrix.Translation(offset) @ hips.matrix
    bpy.context.view_layer.update()
    rotate_pose_bone(arm, hips.name, Quaternion(x_axis, np.radians(spec["hips"]["lean"])))
    for bone, deg in spec["bend"].items():
        if bone in ("Spine1", "Spine2"):
            deg += sway.get("spine", 0.0) * wave * 0.5
        rotate_pose_bone(arm, MX + bone, Quaternion(x_axis, np.radians(deg)))
    for side in ("Right", "Left"):
        for link in ("Arm", "ForeArm", "Hand"):
            d = spec["arms"].get(link)
            if d:
                pose_bone_toward(arm, MX + side + link, side_vec(d, side))
        if sway.get("arm"):
            phase_sign = 1.0 if side == "Right" else -1.0
            rotate_pose_bone(arm, MX + side + "Arm",
                             Quaternion(x_axis, np.radians(sway["arm"] * wave * phase_sign)))

    legs = spec["legs"]
    if spec.get("ankle_lean"):
        # 足首（両足の真ん中）を支点に、上体の形はそのまま体全体を前へ倒す。脚は下の IK で足首に戻す
        pivot = (body_ankle_target(arm, "Right") + body_ankle_target(arm, "Left")) * 0.5
        rot = Matrix.Rotation(np.radians(spec["ankle_lean"]), 4, "X")
        hips.matrix = Matrix.Translation(pivot) @ rot @ Matrix.Translation(-pivot) @ hips.matrix
        bpy.context.view_layer.update()
    for side in ("Right", "Left"):
        if legs["ik"]:
            lift = legs.get("heel_lift", 0.0)
            leg_ik(arm, side, body_ankle_target(arm, side, lift), legs["knee"])
            foot_flat(arm, side, legs["toe_out"], lift)
        else:
            for link in ("UpLeg", "Leg", "Foot"):
                pose_bone_toward(arm, MX + side + link, side_vec(legs[side][link], side))
            if sway.get("leg"):
                phase_sign = 1.0 if side == "Right" else -1.0
                rotate_pose_bone(arm, MX + side + "UpLeg",
                                 Quaternion(x_axis, np.radians(sway["leg"] * wave * phase_sign)))
    if spec.get("tilt"):
        rotate_pose_bone(arm, hips.name, Quaternion(x_axis, np.radians(spec["tilt"])))
    if spec.get("arm_ik"):
        arm_ik(arm, "Right", spec["arm_ik"])


def engine_to_arm(v, scale=1.0):
    """エンジンの座標（正面 +Z）→ アーマチュアの空間（正面 −Y）"""
    return Vector((-v[0], -v[2], v[1])) / scale


# 手の頂点（基本の姿勢、手首から）と、手の長さの向き・手のひらの向き（--body-clips で測る）
HAND_REST = {}


def measure_hand(body, arm, side):
    me = body.data
    group = body.vertex_groups[MX + side + "Hand"].index
    co = np.array([tuple(v.co) for v in me.vertices
                   if any(g.group == group and g.weight > 0.5 for g in v.groups)])
    bone = arm.data.bones[MX + side + "Hand"]
    pts = co - np.array(bone.head_local)
    length = (bone.tail_local - bone.head_local).normalized()
    normal = Vector(HAND_REST_PALM_NORMAL)
    normal = (normal - length * normal.dot(length)).normalized()
    HAND_REST[side] = {"pts": pts, "length": length, "normal": normal}
    log(f"{side} の手：頂点 {len(pts)}・長さの向き {tuple(round(c, 3) for c in length)}・"
        f"手のひらの向き {tuple(round(c, 3) for c in normal)}")


def frame3(forward, up):
    """forward を X、up を Z にする正規直交の行列（列がそれぞれの軸）"""
    x = forward.normalized()
    z = (up - x * up.dot(x)).normalized()
    y = z.cross(x)
    return Matrix((x, y, z)).transposed()


def arm_ik(arm, side, spec):
    """手のひらの上面の中心を spec["palm"] に置き、手のひらを上へ・指先を spec["fingers"] へ向ける。
    腕は 2 本のボーン（Arm・ForeArm）の IK（届かなければ伸び切る＝ログの「届かない」を見て clavicle を足す）"""
    scale = catapult_scale()
    info = HAND_REST[side]
    up = Vector((0.0, 0.0, 1.0))
    fingers = engine_to_arm(spec["fingers"])
    fingers = (fingers - up * fingers.dot(up)).normalized()
    rot = frame3(fingers, up) @ frame3(info["length"], info["normal"]).transposed()   # 手のひらの向き → 上
    pts = np.array([tuple(rot @ Vector(p)) for p in info["pts"]])
    # 手のひらの上面＝回した頂点の中心から、手のひらの向きに厚みの半分
    center = pts.mean(0)
    half = float(np.ptp(np.array([Vector(p).dot(info["normal"]) for p in info["pts"]]))) * 0.5
    wrist = engine_to_arm(spec["palm"], scale) - Vector((center[0], center[1], center[2] + half))

    if spec.get("clavicle"):
        sign = 1.0 if side == "Right" else -1.0
        rotate_pose_bone(arm, MX + side + "Shoulder",
                         Quaternion(Vector((0.0, 0.0, 1.0)), np.radians(spec["clavicle"]) * sign))
    up_arm = arm.pose.bones[MX + side + "Arm"]
    fore = arm.pose.bones[MX + side + "ForeArm"]
    shoulder = up_arm.head.copy()
    l1, l2 = up_arm.bone.length, fore.bone.length
    axis = wrist - shoulder
    reach = axis.length
    d = min(reach, (l1 + l2) * 0.999)
    axis.normalize()
    a = (l1 * l1 - l2 * l2 + d * d) / (2.0 * d)
    h = np.sqrt(max(l1 * l1 - a * a, 0.0))
    hint = engine_to_arm(spec["elbow"])
    perp = (hint - axis * hint.dot(axis)).normalized()
    elbow = shoulder + axis * a + perp * h
    pose_bone_toward(arm, up_arm.name, elbow - shoulder)
    pose_bone_toward(arm, fore.name, shoulder + axis * d - elbow)
    hand = arm.pose.bones[MX + side + "Hand"]
    hand.matrix = Matrix.Translation(hand.head.copy()) @ (rot @ hand.bone.matrix_local.to_3x3()).to_4x4()
    bpy.context.view_layer.update()

    def eng(p):
        return f"({-p[0] * scale:+.2f}, {p[2] * scale:+.2f}, {-p[1] * scale:+.2f})"
    short = reach - (l1 + l2)
    world = pts + np.array(hand.head)
    log(f"手を差し出す（{side}）：肩→手首 {reach * scale:.2f} m／腕の長さ {(l1 + l2) * scale:.2f} m"
        f"{'（届かない ' + format(short * scale, '.2f') + ' m）' if short > 0 else ''}・ひじ {eng(elbow)}・"
        f"手の範囲（エンジン）x {-world[:, 0].max() * scale:+.2f}〜{-world[:, 0].min() * scale:+.2f}・"
        f"y {world[:, 2].min() * scale:+.2f}〜{world[:, 2].max() * scale:+.2f}・"
        f"z {-world[:, 1].max() * scale:+.2f}〜{-world[:, 1].min() * scale:+.2f}")


def capture_pose(arm):
    return {pb.name: pb.matrix_basis.decompose()[:2] for pb in arm.pose.bones}


def apply_blend(arm, a, b, w):
    for pb in arm.pose.bones:
        (la, qa), (lb, qb) = a[pb.name], b[pb.name]
        pb.matrix_basis = Matrix.LocRotScale(la.lerp(lb, w), qa.slerp(qb, w), None)
    bpy.context.view_layer.update()


def pose_clip(arm, clip, t, captured):
    """クリップ clip の時刻 t [秒] の姿勢。captured は keys の姿勢を先に付けて取っておいたもの"""
    spec = BODY_CLIPS[clip]
    if "pose" in spec:
        pose_body(arm, spec["pose"], t / spec["duration"])
        return
    keys = spec["keys"]
    for (t0, p0), (t1, p1) in zip(keys, keys[1:]):
        if t <= t1 or t1 == keys[-1][0]:
            apply_blend(arm, captured[p0], captured[p1], smoothstep((t - t0) / max(t1 - t0, 1e-6)))
            return


def add_body_clips(arm):
    """BODY_CLIPS を毎コマのキーで焼き、NLA に並べる。肩の装甲のボーンは回転コピーの拘束のまま
    （glTF の書き出しはコマごとに拘束込みの姿勢を取るので、キーは打たない）"""
    bpy.ops.object.select_all(action="DESELECT")
    bpy.context.view_layer.objects.active = arm
    bpy.ops.object.mode_set(mode="POSE")
    arm.animation_data_create()
    keyed = [pb for pb in arm.pose.bones if pb.name.startswith(MX)]
    for pb in arm.pose.bones:
        pb.rotation_mode = "QUATERNION"
    captured = {}
    for name in BODY_POSES:
        pose_body(arm, name)
        captured[name] = capture_pose(arm)
    for clip, spec in BODY_CLIPS.items():
        action = bpy.data.actions.new(clip)
        action.use_fake_user = True
        arm.animation_data.action = action
        frames = int(round(spec["duration"] * BODY_ANIM_FPS))
        for f in range(frames + 1):
            pose_clip(arm, clip, f / BODY_ANIM_FPS, captured)
            for pb in keyed:
                pb.keyframe_insert("rotation_quaternion", frame=f)
                pb.keyframe_insert("location", frame=f)
        track = arm.animation_data.nla_tracks.new()
        track.name = clip
        track.strips.new(clip, 0, action)
        track.mute = True
        arm.animation_data.action = None
        log(f"体のクリップ {clip}: {spec['duration']} 秒・{frames + 1} コマ")
    for pb in arm.pose.bones:
        pb.matrix_basis = Matrix()
    bpy.ops.object.mode_set(mode="OBJECT")
    bpy.context.scene.render.fps = BODY_ANIM_FPS
    return captured


def add_box(name, center, size, color):
    bm = bmesh.new()
    bmesh.ops.create_cube(bm, size=1.0)
    bmesh.ops.scale(bm, vec=size, verts=bm.verts)
    bmesh.ops.translate(bm, vec=center, verts=bm.verts)
    me = bpy.data.meshes.new(name)
    bm.to_mesh(me)
    bm.free()
    o = bpy.data.objects.new(name, me)
    o.color = color
    bpy.context.scene.collection.objects.link(o)
    return o


def attach_wings(body_arm, wings_blend):
    """確認用：羽根を体の Spine2 に付ける（基本の姿勢で位置を合わせてからボーンの子にする＝エンジンの BoneSocket と同じ）"""
    got = append_objects(wings_blend, {"PlayerWings", "PlayerWingRig"})
    wing_arm = got["PlayerWingRig"]
    if wing_arm.animation_data:
        for track in wing_arm.animation_data.nla_tracks:
            track.mute = True
    for pb in body_arm.pose.bones:
        pb.matrix_basis = Matrix()
    bpy.context.view_layer.update()
    world = wing_arm.matrix_world.copy()
    wing_arm.parent = body_arm
    wing_arm.parent_type = "BONE"
    wing_arm.parent_bone = MX + "Spine2"
    bpy.context.view_layer.update()
    wing_arm.matrix_world = world
    bpy.context.view_layer.update()
    return list(got.values())


# 確認用の固定台（gen_catapult.py の CatapultClamp と同じ寸法 [m、カタパルトの大きさ]）：溝の幅 1.2・長さ 5.2、
# 足の前後の爪（幅 2.2・高さ 0.5、中心から 2.05〜2.45）
PREVIEW_CLAMP = {"groove_w": 1.2, "half_len": 2.6, "jaw_w": 2.2, "jaw_h": 0.5, "jaw_z": (2.05, 2.45)}


def preview_clamps(scale):
    c = PREVIEW_CLAMP
    objs = [add_box("PreviewFloor", (0.0, 0.0, -0.016), (2.0, 2.0, 0.01), (0.35, 0.35, 0.37, 1.0))]
    for s in (-1, 1):
        x, y = s * CATAPULT_CLAMP[0] / scale, -CATAPULT_CLAMP[1] / scale
        objs.append(add_box(f"PreviewClamp{s}", (x, y, -0.0105), (c["groove_w"] / scale, 2 * c["half_len"] / scale, 0.002),
                            (0.95, 0.75, 0.1, 1.0)))
        for zs in (-1, 1):
            mid = zs * sum(c["jaw_z"]) * 0.5
            objs.append(add_box(f"PreviewJaw{s}{zs}", (x, y - mid / scale, -0.011 + c["jaw_h"] * 0.5 / scale),
                                (c["jaw_w"] / scale, (c["jaw_z"][1] - c["jaw_z"][0]) / scale, c["jaw_h"] / scale),
                                (0.15, 0.15, 0.17, 1.0)))
    return objs


def preview_hub_marks(scale):
    """確認用：HUB の足場（gen_hub_temple.py の BRIDGE_*：上面 10.6・半幅 1.2・先 z 3.85）とコクピットの円柱
    （rig_hub_mech.py：軸 (0, 12.343, 0.465)・半径 1.1・幅 2.0）。エンジンの座標 [m] から"""
    top, half_w, z_end, z_far = 10.605, 1.2, 3.85, 9.0
    c = engine_to_arm((0.0, top - 0.15, (z_end + z_far) * 0.5), scale)
    objs = [add_box("PreviewBridge", c, Vector((2 * half_w, z_far - z_end, 0.3)) / scale, (0.8, 0.78, 0.72, 1.0))]
    bpy.ops.mesh.primitive_cylinder_add(radius=1.1 / scale, depth=2.0 / scale,
                                        location=engine_to_arm((0.0, 12.343, 0.465), scale), rotation=(0.0, np.pi / 2, 0.0))
    cyl = bpy.context.active_object
    cyl.name = "PreviewCockpit"
    cyl.color = (0.1, 0.8, 0.4, 1.0)
    objs.append(cyl)
    return objs


def preview_body_clips(arm, body, wings_blend, captured, out_dir):
    """姿勢ごとに正面・横・斜め後ろ・斜め前から（床の上の姿勢は床と固定台の目印、Fly は開いた羽根を付けて）と、
    Launch のコマを横から並べた 1 枚"""
    out_dir.mkdir(parents=True, exist_ok=True)
    cam = setup_render(1600, 1000)
    sc = bpy.context.scene
    sc.display.shading.color_type = "TEXTURE"
    ground = preview_clamps(catapult_scale())
    wings = attach_wings(arm, wings_blend) if wings_blend and wings_blend.exists() else []
    views = [("front", (0.0, -4.0, 0.85)), ("side", (4.0, 0.0, 0.85)),
             ("back_diag", (-2.6, 3.0, 2.0)), ("front_diag", (2.4, -3.0, 1.2))]
    hub = preview_hub_marks(catapult_scale())
    for name in BODY_POSES:
        pose_body(arm, name)
        flying = name == "Fly"
        for o in wings:
            o.hide_render = not flying
        for o in ground:
            o.hide_render = flying
        for o in hub:
            o.hide_render = name != "Offer"
        center = Vector((0.0, 0.0, 0.85 if flying else 0.75))
        for view, loc in views:
            look(cam, Vector(loc), center, 3.4 if flying else 2.8)
            render(out_dir / f"body_{name}_{view}.png")
        if name == "Offer":
            # 手のまわりを近くから（斜め前・横・足場の上の待機の視点＝エンジンの (0, 12.2, 6.6) から）
            s = catapult_scale()
            palm = engine_to_arm(HUB_OFFER["palm"], s)
            for view, loc in [("hand_diag", palm + Vector((-0.35, -0.45, 0.25))), ("hand_side", palm + Vector((-0.6, 0.05, 0.05)))]:
                look(cam, loc, palm + Vector((0.0, 0.05, 0.05)), 0.9)
                render(out_dir / f"body_{name}_{view}.png")
            cam.data.type = "PERSP"
            cam.data.lens = 24.0
            cam.location = engine_to_arm((0.0, 12.2, 6.6), s)
            cam.rotation_euler = (engine_to_arm((0.0, 11.0, 2.0), s) - cam.location).to_track_quat("-Z", "Y").to_euler()
            render(out_dir / f"body_{name}_eye.png")
            cam.data.type = "ORTHO"
    for o in hub:
        o.hide_render = True
    # 1 回きりのクリップ：横から 7 コマ（羽根は収納＝見えない）
    for o in wings:
        o.hide_render = True
    for o in ground:
        o.hide_render = False
    for clip in [c for c, spec in BODY_CLIPS.items() if "keys" in spec]:
        duration = BODY_CLIPS[clip]["duration"]
        paths = []
        for i in range(7):
            pose_clip(arm, clip, duration * i / 6, captured)
            look(cam, Vector((4.0, -0.3, 0.8)), Vector((0.0, -0.3, 0.8)), 2.8)
            path = out_dir / f"_{clip}_{i}.png"
            render(path)
            paths.append(path)
        join_images(paths, out_dir / f"body_{clip}_side.png")
    for o in wings + ground + hub:
        bpy.data.objects.remove(o, do_unlink=True)
    for pb in arm.pose.bones:
        pb.matrix_basis = Matrix()
    bpy.context.view_layer.update()


def flip_texcoord_v(gltf_path):
    """書き出した glTF の TEXCOORD_0 を v → 1 - v にする（rig_hub_mech.py と同じ。cook は glTF の V を必ず反転するが、
    Blender の glTF の UV はすでに正しいので、cook の反転の逆を書いておく）"""
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
        base = view.get("byteOffset", 0) + acc.get("byteOffset", 0)
        stride = view.get("byteStride", 8)
        v = np.frombuffer(data, dtype="<f4", count=acc["count"] * stride // 4, offset=base).copy()
        v = v.reshape(acc["count"], stride // 4)
        v[:, 1] = 1.0 - v[:, 1]
        data[base:base + v.nbytes] = v.tobytes()
        if "min" in acc and "max" in acc:
            acc["min"][1], acc["max"][1] = 1.0 - acc["max"][1], 1.0 - acc["min"][1]
    bin_path.write_bytes(bytes(data))
    gltf_path.write_text(json.dumps(gltf, indent=2), encoding="utf-8")
    log(f"TEXCOORD_0 の V を反転（{gltf_path.name}）")


def export_skinned(objs, path):
    """メッシュとアーマチュアを glTF（Separate）で。NLA のトラックごとにクリップを書き出し、
    テクスチャは元の PNG（.blend の隣の textures/）をそのまま参照する"""
    for o in bpy.data.objects:
        if o.animation_data:
            for track in o.animation_data.nla_tracks:
                track.mute = False
    bpy.context.scene.frame_set(0)
    bpy.ops.object.select_all(action="DESELECT")
    for o in objs:
        o.select_set(True)
    bpy.context.view_layer.objects.active = objs[0]
    path.parent.mkdir(parents=True, exist_ok=True)
    bpy.ops.export_scene.gltf(filepath=str(path.resolve()), export_format="GLTF_SEPARATE", use_selection=True,
                              export_yup=True, export_apply=False, export_animations=True,
                              export_animation_mode="NLA_TRACKS", export_force_sampling=True,
                              export_optimize_animation_size=False, export_keep_originals=True,
                              export_image_format="AUTO")
    flip_texcoord_v(path)
    log(f"exported {path}")


def body_clips(body_blend, wings_blend, preview_dir, export_dir):
    bpy.ops.wm.open_mainfile(filepath=str(body_blend.resolve()))
    arm, body = bpy.data.objects["PlayerBodyRig"], bpy.data.objects["PlayerBody"]
    for o in [o for o in bpy.data.objects if o.type in ("CAMERA", "LIGHT")]:
        bpy.data.objects.remove(o, do_unlink=True)
    BODY_HEIGHT[0] = float(world_vertices(body)[:, 2].max())
    scale = CATAPULT_SCALE_HEIGHT / BODY_HEIGHT[0]
    log(f"体の全高 {BODY_HEIGHT[0]:.3f} m → カタパルトでは {scale:.3f} 倍。固定台の中心はモデルの "
        f"x ±{CATAPULT_CLAMP[0] / scale:.4f}・前 {CATAPULT_CLAMP[1] / scale:.4f}")
    fit_feet(body, arm)
    measure_hand(body, arm, "Right")
    captured = add_body_clips(arm)
    if preview_dir:
        preview_body_clips(arm, body, wings_blend, captured, preview_dir)
    pose_body(arm, "Catapult")
    for side in ("Right", "Left"):
        foot = arm.pose.bones[MX + side + "Foot"]
        log(f"Catapult の {side} の足首 {tuple(round(c, 4) for c in foot.head)}"
            f"（エンジンの座標 x {-foot.head.x:+.4f}・前 {-foot.head.y:+.4f}）")
    for pb in arm.pose.bones:
        pb.matrix_basis = Matrix()
    bpy.context.view_layer.update()
    if export_dir:
        set_export_material(body_blend.parent)
        export_skinned([body, arm], export_dir / "PlayerModel.gltf")


# 材質の比較（2026-10-18 ユーザー：実機で反射が強すぎて表面がざらざらに見える）。Eevee・アンチエイリアス 1 サンプル
# （エンジンと同じく画素ごとのちらつきが見える）で、空の映り込みと平行光源。胸から上を近くから撮る
MATERIAL_TEST = {
    "grid": {"roughness": (0.055, 0.25, 0.4), "metallic": (0.976, 0.7, 0.4)},
    "normal": (1.0, 0.6, 0.3),            # 法線マップの強さ（2 枚目。金属度・粗さは --metal / --rough、既定は今の値）
}


def set_body_material(metallic, roughness, normal_strength):
    for m in bpy.data.materials:
        if not m.use_nodes or not m.name.startswith("tripo_mat"):
            continue
        for n in m.node_tree.nodes:
            if n.type == "BSDF_PRINCIPLED":
                n.inputs["Metallic"].default_value = metallic
                n.inputs["Roughness"].default_value = roughness
            elif n.type == "NORMAL_MAP":
                n.inputs["Strength"].default_value = normal_strength


# 書き出す体の材質（2026-10-18 ユーザー決定。--material-test の比較で決めた）。.blend の値（LowerLeg にそろえた
# 金属度 0.976・粗さ 0.055）は実機で反射が強すぎて表面がざらざらに見えたので、粗さを上げ、法線マップを弱める。
# エンジンに法線の強さの設定は無いので、平らに寄せた法線マップ <名前>Soft.png を textures/ に作って差し替える（元の PNG は残す）
BODY_EXPORT_MATERIAL = {"metallic": 0.976, "roughness": 0.4, "normal_strength": 0.6}


def soften_normal_maps(blend_dir, strength):
    """体の法線マップを strength 倍に弱めた画像（xy を strength 倍して正規化）を書き、マテリアルの参照を差し替える"""
    done = {}
    for m in bpy.data.materials:
        if not m.use_nodes:
            continue
        for n in m.node_tree.nodes:
            if n.type != "TEX_IMAGE" or not n.image or "NormalMap" not in n.image.filepath:
                continue
            src = n.image
            if src.name not in done:
                w, h = src.size
                px = np.empty(w * h * 4, dtype=np.float32)
                src.pixels.foreach_get(px)
                px = px.reshape(-1, 4)
                v = px[:, :3] * 2.0 - 1.0
                v[:, :2] *= strength
                v /= np.linalg.norm(v, axis=1, keepdims=True)
                px[:, :3] = v * 0.5 + 0.5
                stem = Path(bpy.path.abspath(src.filepath)).stem + "Soft"
                path = (blend_dir / PARTS_TEXTURE_DIR / f"{stem}.png").resolve()
                img = bpy.data.images.new(stem, w, h, alpha=True, is_data=True)
                img.pixels.foreach_set(px.ravel())
                img.file_format = "PNG"
                img.save(filepath=str(path))   # 引数なしの save() は作った画像を書かない
                bpy.data.images.remove(img)
                # 作った画像のままだと glTF の書き出しがファイルとして扱わず法線マップが抜けるので、PNG を読み込み直す
                img = bpy.data.images.load(str(path))
                img.colorspace_settings.name = "Non-Color"
                img.filepath = bpy.path.relpath(str(path), start=str(blend_dir.resolve()))
                done[src.name] = img
                log(f"法線マップ {strength} 倍：{path.name}")
            n.image = done[src.name]


def set_export_material(blend_dir):
    spec = BODY_EXPORT_MATERIAL
    set_body_material(spec["metallic"], spec["roughness"], 1.0)
    if spec["normal_strength"] != 1.0:
        soften_normal_maps(blend_dir, spec["normal_strength"])


def material_test(body_blend, out_dir, metal, rough):
    bpy.ops.wm.open_mainfile(filepath=str(body_blend.resolve()))
    for o in [o for o in bpy.data.objects if o.type in ("CAMERA", "LIGHT")]:
        bpy.data.objects.remove(o, do_unlink=True)
    sc = bpy.context.scene
    sc.render.engine = "BLENDER_EEVEE_NEXT"
    sc.eevee.taa_render_samples = 1
    sc.render.resolution_x, sc.render.resolution_y = 640, 640
    sc.view_settings.view_transform = "Standard"
    world = bpy.data.worlds.new("Sky")
    world.use_nodes = True
    sky = world.node_tree.nodes.new("ShaderNodeTexSky")
    sky.sky_type = "NISHITA"
    sky.sun_elevation = np.radians(35.0)
    world.node_tree.links.new(sky.outputs["Color"], world.node_tree.nodes["Background"].inputs["Color"])
    world.node_tree.nodes["Background"].inputs["Strength"].default_value = 0.3
    sc.world = world
    sun = bpy.data.objects.new("Sun", bpy.data.lights.new("Sun", "SUN"))
    sun.data.energy = 3.0
    sun.rotation_euler = (np.radians(50.0), 0.0, np.radians(30.0))
    sc.collection.objects.link(sun)
    cam = bpy.data.objects.new("Cam", bpy.data.cameras.new("Cam"))
    sc.collection.objects.link(cam)
    sc.camera = cam
    cam.data.lens = 50.0
    cam.location = Vector((-0.55, -1.25, 1.55))
    cam.rotation_euler = (Vector((0.0, 0.0, 1.38)) - cam.location).to_track_quat("-Z", "Y").to_euler()
    out_dir.mkdir(parents=True, exist_ok=True)

    g = MATERIAL_TEST["grid"]
    for mi, metallic in enumerate(g["metallic"]):
        paths = []
        for ri, roughness in enumerate(g["roughness"]):
            set_body_material(metallic, roughness, 1.0)
            path = out_dir / f"_mat_{mi}_{ri}.png"
            render(path)
            paths.append(path)
        join_images(paths, out_dir / f"mat_metal{metallic}_rough{'-'.join(str(r) for r in g['roughness'])}.png")
    paths = []
    for si, strength in enumerate(MATERIAL_TEST["normal"]):
        set_body_material(metal, rough, strength)
        path = out_dir / f"_nrm_{si}.png"
        render(path)
        paths.append(path)
    join_images(paths, out_dir / f"normal_metal{metal}_rough{rough}_strength{'-'.join(str(s) for s in MATERIAL_TEST['normal'])}.png")


def export_wings(wings_blend, export_dir):
    bpy.ops.wm.open_mainfile(filepath=str(wings_blend.resolve()))
    export_skinned([bpy.data.objects["PlayerWings"], bpy.data.objects["PlayerWingRig"]],
                   export_dir / "PlayerWings.gltf")


# ============================================================
def arg_path(argv, name):
    return Path(argv[argv.index(name) + 1]) if name in argv else None


def main():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    src = arg_path(argv, "--src") or Path("Assets/Models/PlayerMech/PlayerMech.glb")
    save_path = arg_path(argv, "--save")
    preview_dir = arg_path(argv, "--preview")

    mirror_arm_path = arg_path(argv, "--mirror-left-arm")
    if mirror_arm_path:
        # 手で直している編集用の .blend の右腕を、左腕の鏡写しに置き換えて同じファイルに保存する
        bpy.ops.wm.open_mainfile(filepath=str(mirror_arm_path.resolve()))
        mirror_chain_in_edit("Body_LeftArm", "Body_RightArm", EDIT_CHAINS["Body_LeftArm"])
        save(mirror_arm_path)
        return

    export_dir = arg_path(argv, "--export")
    clips_body = arg_path(argv, "--body-clips")
    if clips_body:
        wings_blend = arg_path(argv, "--wings-blend") or Path("Assets/Models/Player/PlayerWings/PlayerWings_rig.blend")
        body_clips(clips_body, wings_blend, preview_dir, export_dir)
        return
    material_blend = arg_path(argv, "--material-test")
    if material_blend:
        metal = float(argv[argv.index("--metal") + 1]) if "--metal" in argv else PARTS_METALLIC
        rough = float(argv[argv.index("--rough") + 1]) if "--rough" in argv else PARTS_ROUGHNESS
        material_test(material_blend, preview_dir or Path("."), metal, rough)
        return
    wings_export = arg_path(argv, "--export-wings")
    if wings_export:
        export_wings(wings_export, export_dir or wings_export.parent)
        return

    finish_path = arg_path(argv, "--finish")
    if finish_path:
        # 手で直した編集用の .blend → PlayerBody にまとめ直して保存（編集用のファイルは上書きしない）
        bpy.ops.wm.open_mainfile(filepath=str(finish_path.resolve()))
        finish_edit()
        save(save_path or finish_path.with_name("PlayerMech_rig.blend"))
        return
    if save_path and save_path.exists() and "--force" not in argv:
        raise SystemExit(f"[rig_player_mech] {save_path} はもうある。手で直した分が消えるので止めた（上書きするなら --force）")

    global JOINT_OVERRIDES
    wings_body = arg_path(argv, "--wings")
    if wings_body:
        # 羽根：--src の GLB から羽根を取り出し、--wings の体（PlayerModel_rig.blend）に合わせて --save へ
        rig_wings(src, wings_body, preview_dir, save_path)
        return

    src_blend = arg_path(argv, "--src-blend")
    if src_blend:
        # 組み上げたモデル：パーツの .blend を開いてボーンを入れ、--save へ（テクスチャは隣の textures/ に PNG）
        JOINT_OVERRIDES = PARTS_JOINT_OVERRIDES
        bpy.ops.wm.open_mainfile(filepath=str(src_blend.resolve()))
        objs = [o for o in bpy.data.objects if o.type == "MESH"]
        fix_names(objs)
        rig_parts(objs, preview_dir, save_path)
        return

    bpy.ops.wm.read_factory_settings(use_empty=True)
    objs = import_source(src)
    log(f"{len(objs)} parts from {src}")
    fix_names(objs)

    wings = {o.name: o for o in objs if WING_MARK in o.name}
    body = {o.name: o for o in objs if WING_MARK not in o.name}
    # 羽根は同じ側の羽根どうし、体は体の部位どうしで移す（T ポーズで腕・脚を回すと、別の部位に混ざった破片が置き去りになる）
    for group, same_group in ((wings, lambda a, b: wing_side(a) == wing_side(b)), (body, lambda a, b: True)):
        clean_group(group, same_group)
    report_extents(wings)

    if preview_dir:
        preview(objs, wings, preview_dir)
    if "--rig" in argv:
        rig(objs, preview_dir)
    if save_path:
        save(save_path)


def clean_group(group, same_group):
    names, plans, moved, kept_large = plan_moves(group, same_group)
    log(f"移す島: {len(moved)} 個・{sum(m[2] for m in moved)} 頂点")
    summary = {}
    for src_name, dst_name, n in moved:
        key = (src_name, dst_name)
        count, verts = summary.get(key, (0, 0))
        summary[key] = (count + 1, verts + n)
    for (src_name, dst_name), (count, verts) in sorted(summary.items()):
        log(f"  {src_name:20s} -> {dst_name:20s} 島 {count:3d} 個 / {verts:6d} 頂点")
    for name, n, total, other, d_own, d_other in kept_large:
        log(f"  [動かさない・要確認] {name} の島 {n}/{total} 頂点は {other} の方が近い"
            f"（平均距離 {d_own:.4f} → {d_other:.4f}）")
    apply_moves(group, names, plans)


def save(save_path):
    bpy.ops.file.pack_all()
    save_path.parent.mkdir(parents=True, exist_ok=True)
    # 圧縮しないと 100MB を超えて GitHub に push できない。上書きの控え（.blend1）も作らない
    bpy.context.preferences.filepaths.save_version = 0
    bpy.ops.wm.save_as_mainfile(filepath=str(save_path.resolve()), compress=True)
    log(f"saved {save_path}")


if __name__ == "__main__":
    main()
