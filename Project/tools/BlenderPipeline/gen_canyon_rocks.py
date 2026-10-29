"""谷の障害物（とがった岩・石柱の梁。4_StageGimmick.md / 11_LowAltitudeFlight.md の谷）を作るスクリプト。

使い方（Project/ で実行）:
    blender -b --factory-startup -P tools/BlenderPipeline/gen_canyon_rocks.py -- [オプション]
        --export <フォルダ>    部品ごとの glTF を書き出す（例: Assets/Models/CanyonRocks → cook で Resources へ）
        --preview <フォルダ>   谷の中から見た PNG（とがった岩の並び・交差した梁）
        --save <パス.blend>    組み立てた結果を .blend で保存
        --broken               プレビューを壊れた姿（根元・折れた部分・破片）で撮る

部品（どれも原点 = 谷の飛行高度 Y −60 に置く点。Wave の positions は Y −60 のまま書ける）:
    CanyonSpireL   … とがった岩・大。先は原点の 30m 上、原点での半径 5m（障害物、HP 120）
    CanyonSpireS   … とがった岩・小。先は原点の 14m 上、原点での半径 3m（障害物、HP 60）
    CanyonSpireLow … 低い岩。先は原点の 15m 下で止まる（飾り。当たり判定なし、streamGroup "Canyon"）
    CanyonBeam     … 石柱の梁。ローカル Y に沿った長さ 120m の円柱（両端は谷の壁に埋める）。
                     置くときは rotate_deg = (0, 0, 90 ± 傾き) で横に倒す（Z 軸だけ＝描画と当たり判定の回転順の差が出ない）。
                     たわみはローカル −X 側（Z を +90° 付近に倒すとワールドの下）
岩は下へ 100m 伸ばして谷底（Y −150）に埋める。当たり判定はプレハブのカプセル（飛行高度の太さ）。

座標（Blender）: X=右 / Y=奥 / Z=上。cook は glTF の X だけを反転するので、エンジンでは (x, y, z) = (-X, Z, -Y)。
形はエンジン座標で作って e2b で Blender へ移す。テクスチャは地形の岩の層（gen_terrain_layers.py）を
Assets/Textures/CanyonRocks/ に写した物（谷の壁と同じ岩に見せる）。
地形の層の色画像はアルファに高さが入っているので、写すときは RGB にする（そのままだと岩が半透明になる）。
"""

import math
import sys
from pathlib import Path

import bmesh
import bpy
import numpy as np
from mathutils import Vector

sys.path.insert(0, str(Path(__file__).resolve().parent))
import gen_hub_temple as ht  # noqa: E402  書き出し・材質・撮影の関数を使い回す

LOG = "[gen_canyon_rocks]"
TEX_DIR = Path("Assets/Textures/CanyonRocks")
BURY = 100.0          # 原点（飛行高度）から下へ伸ばす長さ [m]（谷底は 90m 下）
TEX_TILE = 8.0        # テクスチャ 1 枚の大きさ [m]
RING_STEP = 2.0       # 高さ方向の輪の間隔 [m]

# とがった岩：名前、先の高さ T（原点から）、原点での半径 r0、根元（−BURY）の半径 rb、角の数、seed
SPIRES = [
    ("CanyonSpireL", 30.0, 5.0, 14.0, 14, 11),
    ("CanyonSpireS", 14.0, 3.0, 10.0, 12, 23),
    ("CanyonSpireLow", -15.0, None, 11.0, 12, 37),
]
BEAM = {"name": "CanyonBeam", "half_len": 60.0, "radius": 3.5, "end_radius": 4.2, "sag": 1.2, "sides": 14, "seed": 53}

# 壊れた時の部品（ObstacleDebris.json が使う）。元の岩と同じ乱数で作り、ギザギザの切り口で切り分ける。
# 原点は元の岩と同じ（壊れた瞬間にその場で差し替わる）。(部品名, 下の y, 上の y)。y は部品の軸の高さ
SPIRE_PIECES = {
    "CanyonSpireL": [("CanyonSpireL_Stump", None, 1.0), ("CanyonSpireL_Top", 1.0, None)],
    "CanyonSpireS": [("CanyonSpireS_Stump", None, 0.0), ("CanyonSpireS_Top", 0.0, None)],
}
BEAM_STUB = 28.0  # 梁の壁際の根元の長さの境（ローカル y = ±28）。真ん中 0 で折れる
BEAM_PIECES = [("CanyonBeam_StubA", None, -BEAM_STUB), ("CanyonBeam_HalfA", -BEAM_STUB, 0.0),
               ("CanyonBeam_HalfB", 0.0, BEAM_STUB), ("CanyonBeam_StubB", BEAM_STUB, None)]
JAG = 0.35         # 切り口のギザギザの高さ（半径に対する割合）
CHUNK = {"name": "CanyonChunk", "radius": 1.0, "seed": 71}


def e2b(p):
    return (-p[0], -p[2], p[1])


def value_noise(rng, n):
    """周期 n の 1D の滑らかなノイズ（0..1）を返す関数"""
    table = rng.random(n)

    def f(x):
        i = math.floor(x)
        t = x - i
        a, b = table[i % n], table[(i + 1) % n]
        return a + (b - a) * (t * t * (3.0 - 2.0 * t))
    return f


def rock_radius_jitter(rng, sides):
    """角ごとの半径の揺らぎ（-1..1）を高さで変えるための関数群"""
    lanes = [value_noise(rng, 64) for _ in range(sides)]
    strata = value_noise(rng, 64)

    def f(k, y):
        # 角ごとのゆっくりした凹凸＋高さ方向の段（石を積んだ層）
        lane = lanes[k](y / 9.0) * 2.0 - 1.0
        band = strata(y / 5.0)
        step = 0.12 if band > 0.6 else 0.0
        return lane * 0.18 + step
    return f


def ring_at(rings, y):
    """軸の高さ y の輪（中心・半径）を前後の輪から線形に補間する"""
    for (c0, r0, h0), (c1, r1, h1) in zip(rings, rings[1:]):
        if h0 <= y <= h1:
            t = (y - h0) / max(h1 - h0, 1e-6)
            c = tuple(a + (b - a) * t for a, b in zip(c0, c1))
            return (c, r0 + (r1 - r0) * t, y)
    raise ValueError(f"y={y} は輪の範囲の外")


def cut_rings(rings, lo, hi):
    """lo〜hi（None は端まで）の輪だけを残し、切る所に補間した輪を足す。切った端の印も返す"""
    out = [r for r in rings if (lo is None or r[2] > lo + 1e-4) and (hi is None or r[2] < hi - 1e-4)]
    if lo is not None:
        out.insert(0, ring_at(rings, lo))
    if hi is not None:
        out.append(ring_at(rings, hi))
    return out, lo is not None, hi is not None


def jag_offsets(seed, y, sides, r):
    """切り口の高さのギザギザ（両側の部品で同じ値＝切り口がかみ合う）"""
    rng = np.random.default_rng([seed, int(round(y * 100.0)) + 100000])
    return rng.uniform(-1.0, 1.0, sides) * JAG * max(r, 1.0), rng.uniform(-0.5, 0.5) * JAG * max(r, 1.0)


def assign_uv(bm, uv):
    # UV は面ごとに面の向きの平面へ投影（円周で割ると細い所で横に縮んで木の皮に見えたため）。
    # 面の境で模様が切れるが、角の立った岩肌にはその方が合う。部品は縦に作るので v = 高さ（Blender Z）
    for face in bm.faces:
        n = face.normal
        h = math.hypot(n.x, n.y)
        if h < 0.3:
            # 上を向いた面（蓋・切り口）は真上から
            for loop in face.loops:
                loop[uv].uv = (loop.vert.co.x / TEX_TILE, loop.vert.co.y / TEX_TILE)
            continue
        tx, ty = -n.y / h, n.x / h
        for loop in face.loops:
            co = loop.vert.co
            loop[uv].uv = ((co.x * tx + co.y * ty) / TEX_TILE, co.z / TEX_TILE)


def finish_mesh(name, bm, uv, mat):
    bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=1e-4)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    # 下を塞がない（谷底に埋める）開いた形だと recalc が内向きにそろえることがある（実機で裏返って透けた）。
    # 中心から見て面が外を向いているかを全体で確かめ、内向きなら全部裏返す
    bm.normal_update()
    center = sum((v.co for v in bm.verts), Vector()) / max(len(bm.verts), 1)
    if sum(f.normal.dot(f.calc_center_median() - center) * f.calc_area() for f in bm.faces) < 0.0:
        bmesh.ops.reverse_faces(bm, faces=bm.faces)
    assign_uv(bm, uv)
    mesh = bpy.data.meshes.new(name)
    bm.to_mesh(mesh)
    bm.free()
    for poly in mesh.polygons:
        poly.use_smooth = False  # 角の立った岩肌に見せる
    mesh.materials.append(mat)
    obj = bpy.data.objects.new(name, mesh)
    bpy.context.scene.collection.objects.link(obj)
    return obj


def build_tube(name, rings, sides, rng, mat, cut=None, seed=0):
    """rings: [(中心 (x,y,z), 半径, 軸の高さ y)] を下から。先が 0 半径なら頂点 1 つで閉じる。
    cut = (lo, hi) なら軸の高さ lo〜hi だけを作り、切った端はギザギザの切り口で塞ぐ（乱数の使い方は切らない時と同じ）"""
    bm = bmesh.new()
    uv = bm.loops.layers.uv.new("UVMap")
    jitter = rock_radius_jitter(rng, sides)
    phase = rng.random() * 2.0 * math.pi
    cut_lo = cut_hi = False
    if cut is not None:
        rings, cut_lo, cut_hi = cut_rings(rings, cut[0], cut[1])
    jags = {}
    for idx, flag in ((0, cut_lo), (len(rings) - 1, cut_hi)):
        if flag:
            jags[idx] = jag_offsets(seed, rings[idx][2], sides, rings[idx][1])
    verts = []
    for i, (c, r, h) in enumerate(rings):
        row = []
        for k in range(sides):
            a = phase + 2.0 * math.pi * k / sides
            rr = r * (1.0 + jitter(k, h)) if r > 0.0 else 0.0
            dy = jags[i][0][k] if i in jags else 0.0
            p = (c[0] + math.cos(a) * rr, c[1] + dy, c[2] + math.sin(a) * rr)
            row.append(bm.verts.new(e2b(p)))
        verts.append(row)
    bm.verts.ensure_lookup_table()
    # 切り口は中心へ扇で塞ぐ（中心も少し上下させて割れた面に見せる）
    for i, (c, r, h) in enumerate(rings):
        if i not in jags:
            continue
        center = bm.verts.new(e2b((c[0], c[1] + jags[i][1], c[2])))
        row = verts[i]
        for k in range(sides):
            try:
                bm.faces.new([row[k], row[(k + 1) % sides], center])
            except ValueError:
                pass

    for i in range(len(rings) - 1):
        for k in range(sides):
            k2 = (k + 1) % sides
            try:
                bm.faces.new([verts[i][k], verts[i][k2], verts[i + 1][k2], verts[i + 1][k]])
            except ValueError:
                continue
    # 下の端は谷底・壁に埋めるので塞がない。上の端は先が尖っていなければ蓋をする
    if rings[-1][1] > 0.0 and not cut_hi:
        bm.faces.new(verts[-1])
    if rings[0][1] > 0.0 and "Beam" in name and not cut_lo:
        bm.faces.new(list(reversed(verts[0])))
    return finish_mesh(name, bm, uv, mat)


def build_spire(name, tip, r0, rb, sides, seed, mat, cut=None):
    """根元 −BURY（半径 rb）から先 tip（半径 0）へ。r = rb·u^p（u = 先からの割合）で、原点の半径が r0 になる p を選ぶ"""
    rng = np.random.default_rng(seed)
    total = tip + BURY
    if r0 is None:
        p = 0.8
    else:
        p = math.log(r0 / rb) / math.log(tip / total)
    lean = value_noise(rng, 16)
    lean_dir = rng.random() * 2.0 * math.pi
    rings = []
    n = max(int(total / RING_STEP), 8)
    for i in range(n + 1):
        y = -BURY + total * i / n
        u = (tip - y) / total
        r = rb * (u ** p) if i < n else 0.0
        # 上ほど少し傾く（まっすぐな円すいに見せない）
        off = (lean(i / n * 3.0) - 0.5) * 2.0 * (1.0 - u) * 2.5
        c = (math.cos(lean_dir) * off, y, math.sin(lean_dir) * off)
        rings.append((c, r, y))
    return build_tube(name, rings, sides, rng, mat, cut, seed)


def build_beam(cfg, mat, name=None, cut=None):
    rng = np.random.default_rng(cfg["seed"])
    hl = cfg["half_len"]
    rings = []
    n = int(2.0 * hl / RING_STEP)
    for i in range(n + 1):
        y = -hl + 2.0 * hl * i / n
        s = y / hl
        # 両端（壁に埋まる側）は太く、中央はローカル −X へたわむ
        end = max(abs(s) - 0.8, 0.0) / 0.2
        r = cfg["radius"] + (cfg["end_radius"] - cfg["radius"]) * end * end
        c = (-cfg["sag"] * (1.0 - s * s), y, 0.0)
        rings.append((c, r, y))
    return build_tube(name or cfg["name"], rings, cfg["sides"], rng, mat, cut, cfg["seed"])


def build_chunk(cfg, mat):
    """崩れた時に飛ぶ小さな岩（半径 1m。エンジンで大きさを変えて使う）"""
    rng = np.random.default_rng(cfg["seed"])
    bm = bmesh.new()
    uv = bm.loops.layers.uv.new("UVMap")
    bmesh.ops.create_icosphere(bm, subdivisions=1, radius=cfg["radius"])
    for v in bm.verts:
        v.co *= 0.75 + 0.5 * rng.random()
        v.co.y *= 0.8   # 少し平たく
        v.co.z *= 0.65
    return finish_mesh(cfg["name"], bm, uv, mat)


def build_pieces(mat):
    out = []
    for name, tip, r0, rb, sides, seed in SPIRES:
        for piece, lo, hi in SPIRE_PIECES.get(name, []):
            out.append(build_spire(piece, tip, r0, rb, sides, seed, mat, (lo, hi)))
    for piece, lo, hi in BEAM_PIECES:
        out.append(build_beam(BEAM, mat, piece, (lo, hi)))
    out.append(build_chunk(CHUNK, mat))
    return out


def make_material():
    base = bpy.data.images.load(str((TEX_DIR / "CanyonRock_BaseColor.png").resolve()))
    normal = bpy.data.images.load(str((TEX_DIR / "CanyonRock_NormalMap.png").resolve()))
    normal.colorspace_settings.name = "Non-Color"
    return ht.textured_material("CanyonRock", base, normal, 0.9)


def setup_preview():
    scene = bpy.context.scene
    world = bpy.data.worlds.new("Sky")
    scene.world = world
    world.use_nodes = True
    world.node_tree.nodes["Background"].inputs["Color"].default_value = (0.55, 0.65, 0.8, 1.0)
    world.node_tree.nodes["Background"].inputs["Strength"].default_value = 0.8
    sun = bpy.data.objects.new("Sun", bpy.data.lights.new("Sun", "SUN"))
    sun.data.energy = 4.0
    sun.rotation_euler = (math.radians(50.0), 0.0, math.radians(30.0))
    scene.collection.objects.link(sun)
    # 谷底の目安の床（Y −150 = 原点の 90m 下）
    bpy.ops.mesh.primitive_plane_add(size=400.0, location=e2b((0.0, -90.0, 60.0)))


def place_copy(obj, pos, rot_z_deg=0.0):
    dup = obj.copy()
    bpy.context.scene.collection.objects.link(dup)
    dup.location = e2b(pos)
    # エンジンの Z 軸まわり = Blender の Y 軸まわり（向きは e2b で反転）
    dup.rotation_euler = (0.0, math.radians(rot_z_deg), 0.0)
    return dup


def main():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    export_dir = ht.arg_path(argv, "--export")
    preview_dir = ht.arg_path(argv, "--preview")
    save_path = ht.arg_path(argv, "--save")

    bpy.ops.wm.read_factory_settings(use_empty=True)
    mat = make_material()
    parts = [build_spire(name, tip, r0, rb, sides, seed, mat) for name, tip, r0, rb, sides, seed in SPIRES]
    parts.append(build_beam(BEAM, mat))
    pieces = build_pieces(mat)
    ht.report_stats(parts + pieces)

    if export_dir is not None:
        ht.export_parts(export_dir, parts + pieces)

    if save_path is not None:
        save_path.parent.mkdir(parents=True, exist_ok=True)
        bpy.ops.wm.save_as_mainfile(filepath=str(save_path.resolve()))
    if preview_dir is None:
        return

    # 元の部品は脇へよけ、写しを並べて撮る（原点 = 飛行高度。カメラは原点の高さ）
    spire_l, spire_s, spire_low, beam = parts
    for obj in parts + pieces:
        obj.location = e2b((0.0, -1000.0, 0.0))
    broken = "--broken" in argv
    if broken:
        # 壊れた姿：根元は残し、上・梁の半分は少しずらして傾け、破片を散らす（切り口の確認用）
        by = {o.name: o for o in pieces}
        place_copy(by["CanyonSpireL_Stump"], (-6.0, 0.0, 60.0))
        place_copy(by["CanyonSpireL_Top"], (-9.0, 1.5, 60.0), 20.0)
        place_copy(by["CanyonSpireS_Stump"], (7.0, 0.0, 95.0))
        place_copy(by["CanyonSpireS_Top"], (9.0, 1.0, 95.0), -25.0)
        place_copy(by["CanyonBeam_StubA"], (0.0, 0.0, 180.0), 90.0)
        place_copy(by["CanyonBeam_StubB"], (0.0, 0.0, 180.0), 90.0)
        place_copy(by["CanyonBeam_HalfA"], (0.0, -3.0, 180.0), 80.0)
        place_copy(by["CanyonBeam_HalfB"], (0.0, -3.0, 180.0), 100.0)
        for i, (x, y, z) in enumerate([(-4, -2, 58), (-7, -5, 63), (3, -6, 178), (-2, -9, 182), (5, -1, 93)]):
            place_copy(by["CanyonChunk"], (x, y, z), i * 37.0)
    if not broken:
        place_copy(spire_l, (-6.0, 0.0, 60.0))
        place_copy(spire_s, (7.0, 0.0, 95.0))
        place_copy(beam, (0.0, 0.0, 180.0), 90.0 + 22.0)
        place_copy(beam, (0.0, 0.0, 180.0), 90.0 - 22.0)
    place_copy(spire_low, (-14.0, 0.0, 130.0))
    place_copy(spire_low, (16.0, 0.0, 75.0))
    setup_preview()
    preview_dir.mkdir(parents=True, exist_ok=True)
    ht.setup_render(1280, 720, 32)
    scene = bpy.context.scene
    scene.view_settings.exposure = 0.0
    scene.render.image_settings.file_format = "PNG"
    views = {
        # 実機に近い視野（FovY 0.45 rad ≈ 25.8°）でレールの上から
        "rail": ((0.0, 0.0, 0.0), (0.0, -2.0, 100.0), 25.8),
        # 引いて全体
        "wide": ((40.0, 25.0, -20.0), (0.0, -20.0, 110.0), 50.0),
    }
    for name, (eye, target, fov) in views.items():
        ht.set_camera(eye, target, fov)
        ht.render(preview_dir / f"canyon_rocks_{name}{'_broken' if broken else ''}.png")


if __name__ == "__main__":
    main()
