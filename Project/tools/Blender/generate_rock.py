"""
ボス必殺技で浮かせて割る大岩のモデルを生成する（Blender 4.x をバックグラウンドで実行）。

作り方:
  1. 細かいイコ球を、大きいうねり → 平面で削る（角ばった面）→ 細かいノイズと溝 の順で変形
  2. ポリゴン削減で target-tris 前後へ
  3. 原点を包む箱の中心へ、UV 展開、角度で鋭い辺を決めて glb へ書き出す
エンジン側でボロノイ分割して断面を作るので、穴の無い閉じたメッシュ（全辺が 2 面に接する）であることを確認して出力する。
テクスチャは三平面投影で貼る前提なので glb にマテリアルは入れない（UV は予備として入れる）。
大きさは包む半径 ≒ 1（Blender の Z 上 → glb の Y 上）。ゲーム側でスケールする。

使い方:
  blender.exe -b -P Project/Tools/Blender/generate_rock.py -- \
      --out Project/Assets/Models/Rocks --seeds 1 2 3 \
      --tex Project/Assets/Textures/Rocks --preview <プレビュー画像のパス>
"""
import argparse
import math
import os
import random
import sys

import bmesh
import bpy
from mathutils import Vector, noise


def parse_args():
	argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
	ap = argparse.ArgumentParser()
	ap.add_argument("--out", required=True, help="glb を書き出すフォルダ")
	ap.add_argument("--seeds", type=int, nargs="+", default=[1, 2, 3])
	ap.add_argument("--target-tris", type=int, default=3000)
	ap.add_argument("--name", default="BossRock")
	ap.add_argument("--tex", default="", help="プレビューで貼るテクスチャのフォルダ（空ならプレビューは単色）")
	ap.add_argument("--preview", default="", help="プレビュー画像の出力パス（空なら撮らない）")
	return ap.parse_args(argv)


def random_unit(rng):
	z = rng.uniform(-1.0, 1.0)
	a = rng.uniform(0.0, math.tau)
	r = math.sqrt(1.0 - z * z)
	return Vector((r * math.cos(a), r * math.sin(a), z))


def shape_vertex(v, rng_state):
	"""単位球上の点 v を岩の表面の点へ動かす"""
	off, planes, scale = rng_state
	dir_ = v.normalized()

	# 大きいうねり
	p = dir_ * (1.0 + 0.22 * noise.fractal(dir_ * 1.1 + off, 1.0, 2.0, 4))

	# 平面で削って角ばった面を作る（平面の外へ出た分を平面へ押し戻す）
	for n, d in planes:
		over = p.dot(n) - d
		if over > 0.0:
			p -= n * over

	# 細かいノイズと浅い溝（ボロノイの境界）
	fine = 0.025 * noise.fractal(dir_ * 4.0 + off * 1.7, 1.0, 2.0, 3)
	dists, _ = noise.voronoi(dir_ * 2.6 + off * 0.5, distance_metric="DISTANCE")
	groove = -0.035 * max(0.0, 1.0 - (dists[1] - dists[0]) / 0.09)
	p += dir_ * (fine + groove)

	return Vector((p.x * scale.x, p.y * scale.y, p.z * scale.z))


def build_rock(name, seed, target_tris):
	rng = random.Random(seed)
	off = Vector((rng.uniform(-50, 50), rng.uniform(-50, 50), rng.uniform(-50, 50)))
	planes = [(random_unit(rng), rng.uniform(0.62, 0.86)) for _ in range(rng.randint(7, 11))]
	# 盾として陰に隠れられるよう、高さ（Z）は横幅に比べて低くしすぎない
	scale = Vector((rng.uniform(1.0, 1.3), rng.uniform(0.85, 1.1), rng.uniform(0.95, 1.25)))

	bpy.ops.mesh.primitive_ico_sphere_add(subdivisions=6, radius=1.0)
	obj = bpy.context.active_object
	obj.name = name
	obj.data.name = name

	bm = bmesh.new()
	bm.from_mesh(obj.data)
	state = (off, planes, scale)
	for v in bm.verts:
		v.co = shape_vertex(v.co, state)
	bm.to_mesh(obj.data)
	bm.free()

	src_tris = len(obj.data.polygons)
	dec = obj.modifiers.new("Decimate", "DECIMATE")
	dec.decimate_type = "COLLAPSE"
	dec.ratio = min(1.0, target_tris / src_tris)
	dec.use_collapse_triangulate = True
	bpy.ops.object.modifier_apply(modifier=dec.name)

	# 原点を包む箱の中心へ。包む半径を 1 にそろえる
	bpy.ops.object.origin_set(type="ORIGIN_GEOMETRY", center="BOUNDS")
	obj.location = (0.0, 0.0, 0.0)
	radius = max(v.co.length for v in obj.data.vertices)
	obj.scale = (1.0 / radius,) * 3
	bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)

	try:
		bpy.ops.object.shade_smooth_by_angle(angle=math.radians(35.0))
	except Exception:
		bpy.ops.object.shade_smooth()

	bpy.ops.object.mode_set(mode="EDIT")
	bpy.ops.mesh.select_all(action="SELECT")
	try:
		bpy.ops.uv.smart_project(angle_limit=math.radians(66.0), island_margin=0.02)
	except Exception:
		bpy.ops.uv.cube_project()
	bpy.ops.object.mode_set(mode="OBJECT")
	return obj


def check_closed(obj):
	"""全辺が 2 面に接する閉じたメッシュかを調べる（ボロノイ分割の断面を作るのに必要）"""
	bm = bmesh.new()
	bm.from_mesh(obj.data)
	bad_edges = sum(1 for e in bm.edges if not e.is_manifold)
	tris = len(bm.faces)
	verts = len(bm.verts)
	bm.free()
	return bad_edges, tris, verts


def export_glb(obj, path):
	bpy.ops.object.select_all(action="DESELECT")
	obj.select_set(True)
	bpy.context.view_layer.objects.active = obj
	bpy.ops.export_scene.gltf(
		filepath=path,
		export_format="GLB",
		use_selection=True,
		export_yup=True,
		export_apply=True,
		export_normals=True,
		export_texcoords=True,
		export_materials="NONE",
	)
	print("wrote", path)


def make_preview_material(tex_dir):
	"""三平面投影（Box 投影）で外側テクスチャを貼るプレビュー用マテリアル"""
	mat = bpy.data.materials.new("RockPreview")
	mat.use_nodes = True
	nt = mat.node_tree
	# ノード名は UI 言語で翻訳されるので種類で探す
	bsdf = next(n for n in nt.nodes if n.type == "BSDF_PRINCIPLED")
	bsdf.inputs["Roughness"].default_value = 0.85
	base_path = os.path.join(tex_dir, "RockExterior_BaseColor.png") if tex_dir else ""
	if not base_path or not os.path.exists(base_path):
		bsdf.inputs["Base Color"].default_value = (0.08, 0.08, 0.08, 1.0)
		return mat

	coord = nt.nodes.new("ShaderNodeTexCoord")
	mapping = nt.nodes.new("ShaderNodeMapping")
	mapping.inputs["Scale"].default_value = (0.8, 0.8, 0.8)
	nt.links.new(coord.outputs["Object"], mapping.inputs["Vector"])

	def box_image(path, non_color):
		node = nt.nodes.new("ShaderNodeTexImage")
		node.image = bpy.data.images.load(os.path.abspath(path))
		if non_color:
			node.image.colorspace_settings.name = "Non-Color"
		node.projection = "BOX"
		node.projection_blend = 0.25
		nt.links.new(mapping.outputs["Vector"], node.inputs["Vector"])
		return node

	base = box_image(base_path, False)
	nt.links.new(base.outputs["Color"], bsdf.inputs["Base Color"])
	normal_path = os.path.join(tex_dir, "RockExterior_NormalMap.png")
	if os.path.exists(normal_path):
		# Box 投影では接空間がずれるので、法線マップは凹凸ノードの代用として弱めに使う
		nimg = box_image(normal_path, True)
		bump = nt.nodes.new("ShaderNodeBump")
		bump.inputs["Strength"].default_value = 0.35
		sep = nt.nodes.new("ShaderNodeSeparateColor")
		nt.links.new(nimg.outputs["Color"], sep.inputs["Color"])
		nt.links.new(sep.outputs["Red"], bump.inputs["Height"])
		nt.links.new(bump.outputs["Normal"], bsdf.inputs["Normal"])
	return mat


def render_preview(objs, tex_dir, path):
	scene = bpy.context.scene
	mat = make_preview_material(tex_dir)
	spacing = 2.6
	start = -spacing * (len(objs) - 1) * 0.5
	for i, obj in enumerate(objs):
		obj.data.materials.clear()
		obj.data.materials.append(mat)
		obj.location = (start + spacing * i, 0.0, 0.0)
		obj.rotation_euler = (0.0, 0.0, math.radians(25.0 * i))

	world = bpy.data.worlds.new("PreviewWorld")
	world.use_nodes = True
	bg = next(n for n in world.node_tree.nodes if n.type == "BACKGROUND")
	bg.inputs["Color"].default_value = (0.55, 0.48, 0.35, 1.0)
	bg.inputs["Strength"].default_value = 0.6
	scene.world = world

	sun_data = bpy.data.lights.new("Sun", type="SUN")
	sun_data.energy = 4.0
	sun = bpy.data.objects.new("Sun", sun_data)
	sun.rotation_euler = (math.radians(50.0), math.radians(10.0), math.radians(-30.0))
	scene.collection.objects.link(sun)

	cam_data = bpy.data.cameras.new("Cam")
	cam_data.lens = 40.0
	cam = bpy.data.objects.new("Cam", cam_data)
	scene.collection.objects.link(cam)
	width = spacing * len(objs)
	cam.location = Vector((0.0, -width * 1.05, width * 0.28))
	cam.rotation_euler = (Vector((0.0, 0.0, 0.0)) - cam.location).to_track_quat("-Z", "Y").to_euler()
	scene.camera = cam

	engines = [e.identifier for e in bpy.types.RenderSettings.bl_rna.properties["engine"].enum_items]
	scene.render.engine = "BLENDER_EEVEE_NEXT" if "BLENDER_EEVEE_NEXT" in engines else "BLENDER_EEVEE"
	scene.render.resolution_x = 1600
	scene.render.resolution_y = 600
	scene.render.film_transparent = False
	scene.render.filepath = path
	bpy.ops.render.render(write_still=True)
	print("wrote", path)


def main():
	args = parse_args()
	bpy.ops.wm.read_factory_settings(use_empty=True)
	os.makedirs(args.out, exist_ok=True)

	objs = []
	ok = True
	for i, seed in enumerate(args.seeds):
		name = f"{args.name}_{chr(ord('A') + i)}"
		obj = build_rock(name, seed, args.target_tris)
		bad, tris, verts = check_closed(obj)
		print(f"[{name}] seed={seed} tris={tris} verts={verts} non-manifold-edges={bad}")
		if bad != 0:
			ok = False
		export_glb(obj, os.path.join(args.out, f"{name}.glb"))
		objs.append(obj)

	if args.preview:
		render_preview(objs, args.tex, args.preview)

	if not ok:
		print("WARNING: 閉じていないメッシュがある（ボロノイ分割の断面が欠ける可能性）")


main()
