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


def back_surface_y(points, center_z, half):
    """背中（+Y）の表面：Spine2 の高さのまわりの、真ん中あたりの頂点のいちばん後ろ（95%）"""
    slab = points[(np.abs(points[:, 2] - center_z) < half) & (np.abs(points[:, 0]) < half)]
    return float(np.percentile(slab[:, 1], 95)) if len(slab) else float(points[:, 1].max())


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
        fold_test(wing_arm)
        for name, loc, ortho in (("wings_fold_back", (0.0, 3.0, 0.33), 1.6), ("wings_fold_top", (0.0, 0.001, 3.0), 1.6)):
            look(cam, uv3(loc), center, u(ortho))
            render(preview_dir / f"{name}.png")
        for pb in wing_arm.pose.bones:
            pb.matrix_basis = Matrix()
        bpy.context.view_layer.update()

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
