"""飛行ユニット（20_Loadout.md §3）の Tripo モデルを cook できる形にするスクリプト。

使い方（Project/ で実行）:
    blender -b --factory-startup -P tools/BlenderPipeline/prep_flight_units.py [-- 名前の頭 ...]
    （-- の後に名前の頭を書くとそれだけ。例: -- Defense）

Assets/Models/Weapons/FlightUnit/<フォルダ>/*.glb（どれもメッシュ 1・マテリアル 1・画像 3 枚、長い辺が約 1m に正規化済み）を読み、
  - 前を +Z へ回す（ファンネル小は先が +X を向いているので Y まわりに回す。バックパックは装甲の面が +Z）
  - 包む箱の中心を原点へ（背中・バックパックに付けるずらしは FlightUnit.json の backpack / mounts で合わせる）
  - 金属度・粗さのテクスチャを係数（中央値）に置き換え、色と法線のテクスチャを PNG で書き出す（prep_shields.py と同じ）
して <フォルダ>/Export/<名前>.gltf に書き出す。その後 `python tools/Python/cook_assets.py` で Resources へ。

座標：cook は glTF の X だけを反転するので、エンジンでは前が +Z、上が +Y。
"""

import json
import math
import struct
import zlib
from pathlib import Path

import bpy
import numpy as np
from mathutils import Matrix, Vector

LOG = "[prep_flight_units]"
ROOT = Path("Assets/Models/Weapons/FlightUnit")
# 名前: (フォルダ, 元の glb, 前を +Z にする Blender の Z まわりの回転[度], 他の指定)
# glTF の +Z は Blender の -Y、glTF の +X は Blender の +X
# 他の指定: roll＝表から見て時計回りの回転[度]（Blender の Y まわり）／scale＝拡大／mirror＝反転（True か "X"＝左右、"Y"＝前後、"XY"＝両方）
#           pivot="top"＝原点を上の端へ（コードでそこを支点に回す）／tex＝テクスチャを共有する元の名前（反転したパーツ用）
# 防御系（20 番 §3.2）：展開図（ゲーム内展開モデル）の並びで、回転なしにそろう向き・大きさにする（Main の長さ約 1m が基準）。
#   FrontRight / BackRight は表が Main と逆だったので Z まわりに 180°、BackRight は寝ていたので起こす。左は右の鏡写し
DEFENSE = "Defense"
MOBILITY = "Mobility"
UNITS = {
    "FlightUnitBackpack": ("Backpack", "Backpack.glb", 0.0, {}),
    "FunnelLarge": ("Attack", "LargeFannel.glb", 0.0, {}),
    "FunnelSmall": ("Attack", "SmallFannel.glb", -90.0, {}),
    "DefenseMain": (DEFENSE, "FlightUnit_Defense_Main.glb", 0.0, {}),
    "DefenseFrontRight": (DEFENSE, "FlightUnit_Defense_FrontRight.glb", 180.0, {"scale": 0.36}),
    "DefenseBackRight": (DEFENSE, "FlightUnit_Defense_BackRight.glb", 180.0, {"roll": 90.0, "scale": 0.5}),
    "DefenseFrontLeft": (DEFENSE, "FlightUnit_Defense_FrontRight.glb", 180.0, {"scale": 0.36, "mirror": True}),
    "DefenseBackLeft": (DEFENSE, "FlightUnit_Defense_BackRight.glb", 180.0, {"roll": 90.0, "scale": 0.5, "mirror": True}),
    # 機動系（20 番 §3.3）：背中の左右に 3 枚重ねの可変ウィング。どれも縦に立った板（上が +Y、面が ±Z）で、上の端を支点に扇のように開く。
    #   左の束：奥（体の側）＝OutBack（OutWing を前後反転）／中＝Middle／手前（カメラの側）＝Out。右の束は左の左右反転
    "MobilityOut": (MOBILITY, "LeftOutWing.glb", 0.0, {"pivot": "top"}),
    "MobilityMiddle": (MOBILITY, "LefrMiddleWing.glb", 0.0, {"pivot": "top"}),
    "MobilityOutBack": (MOBILITY, "LeftOutWing.glb", 0.0, {"pivot": "top", "mirror": "Y", "tex": "MobilityOut"}),
    "MobilityOutR": (MOBILITY, "LeftOutWing.glb", 0.0, {"pivot": "top", "mirror": "X", "tex": "MobilityOut"}),
    "MobilityMiddleR": (MOBILITY, "LefrMiddleWing.glb", 0.0, {"pivot": "top", "mirror": "X", "tex": "MobilityMiddle"}),
    "MobilityOutBackR": (MOBILITY, "LeftOutWing.glb", 0.0, {"pivot": "top", "mirror": "XY", "tex": "MobilityOut"}),
}


def log(msg):
    print(f"{LOG} {msg}")


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


def import_unit(name, folder, file, yaw_deg, opts):
    before = set(bpy.data.objects)
    bpy.ops.import_scene.gltf(filepath=str(ROOT / folder / file))
    new = [o for o in bpy.data.objects if o not in before]
    mesh = next(o for o in new if o.type == "MESH")
    for o in new:
        if o is not mesh:
            bpy.data.objects.remove(o)
    mesh.location = (0, 0, 0)
    mesh.rotation_euler = (0, 0, 0)
    mesh.name = mesh.data.name = name
    if yaw_deg != 0.0:
        mesh.data.transform(Matrix.Rotation(math.radians(yaw_deg), 4, "Z"))
    if opts.get("roll", 0.0) != 0.0:
        # 表（Blender の -Y 側）から見て時計回り
        mesh.data.transform(Matrix.Rotation(math.radians(opts["roll"]), 4, "Y"))
    if opts.get("scale", 1.0) != 1.0:
        mesh.data.transform(Matrix.Scale(opts["scale"], 4))
    mirror = opts.get("mirror")
    if mirror:
        axes = "X" if mirror is True else mirror
        for axis in axes:
            mesh.data.transform(Matrix.Scale(-1.0, 4, Vector((1.0, 0.0, 0.0)) if axis == "X" else Vector((0.0, 1.0, 0.0))))
        # 反転が奇数回なら面の巡りを戻す（法線は transform で反転済み）
        if len(axes) % 2 == 1:
            mesh.data.flip_normals()
    check_winding(mesh, name)
    # 包む箱の中心を原点へ
    vs = [v.co for v in mesh.data.vertices]
    lo = Vector((min(v.x for v in vs), min(v.y for v in vs), min(v.z for v in vs)))
    hi = Vector((max(v.x for v in vs), max(v.y for v in vs), max(v.z for v in vs)))
    center = (lo + hi) * 0.5
    if opts.get("pivot") == "top":
        # 上の端（Blender の Z の最大＝エンジンの +Y）の真ん中を原点に
        center = Vector((center.x, center.y, hi.z))
    mesh.data.transform(Matrix.Translation(-center))
    log(f"{name}: size={tuple(round(c, 3) for c in (hi - lo))} center moved by {tuple(round(c, 3) for c in -center)}")
    return mesh


def check_winding(obj, name):
    """面の巡り（表の向き）と頂点の法線が同じ側を向いているか（鏡写しの確認）"""
    me = obj.data
    cn = me.corner_normals
    good = 0
    for poly in me.polygons:
        n = poly.normal
        s = sum(n.dot(cn[i].vector) for i in poly.loop_indices)
        good += 1 if s > 0.0 else 0
    log(f"{name}: winding agrees with normals on {good}/{len(me.polygons)} faces")


def prepare_material(obj, name, tex_name=None):
    mat = obj.data.materials[0]
    mat.name = name
    bsdf, base = tex_node_into(mat, "Base Color")
    _, normal = tex_node_into(mat, "Normal")
    _, mr = tex_node_into(mat, "Roughness")
    # tex_name があれば、そのパーツと同じ名前の画像にする（同じ PNG→DDS を共有する）
    base.image.name = f"{tex_name or name}_BaseColor"
    normal.image.name = f"{tex_name or name}_NormalMap"
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
    log(f"{name}: metallic={metal} roughness={rough}")
    return {"base": base.image, "normal": normal.image}


def write_png(path, px):
    """8bit RGB の PNG を書く（glb に埋め込まれた画像は Image.save() だと元の JPEG のまま書かれるため、ピクセルから書く）"""
    rgb = (np.clip(px[::-1, :, :3], 0.0, 1.0) * 255.0 + 0.5).astype(np.uint8)
    h, w, _ = rgb.shape
    raw = b"".join(bytes(1) + rgb[y].tobytes() for y in range(h))

    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    path.write_bytes(bytes.fromhex("89504e470d0a1a0a") + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
                     + chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))


def save_textures_as_png(info, out_dir):
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
        log(f"texture {path}")


def flip_texcoord_v(gltf_path):
    """cook は glTF の V を必ず反転するが、Blender の glTF の UV はすでに左上が原点なので、反転の逆を書いておく"""
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
    bpy.ops.object.select_all(action="DESELECT")
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    bpy.ops.export_scene.gltf(filepath=str(path.resolve()), export_format="GLTF_SEPARATE", use_selection=True,
                              export_yup=True, export_apply=True, export_animations=False,
                              export_image_format="AUTO")
    flip_texcoord_v(path)
    log(f"exported {path}")


def main():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    import sys
    picks = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    for name, (folder, file, yaw, opts) in UNITS.items():
        if picks and not any(name.startswith(p) for p in picks):
            continue
        obj = import_unit(name, folder, file, yaw, opts)
        info = prepare_material(obj, name, opts.get("tex"))
        out_dir = ROOT / folder / "Export"
        out_dir.mkdir(parents=True, exist_ok=True)
        save_textures_as_png(info, out_dir)
        export_object(obj, out_dir / f"{name}.gltf")
        bpy.data.objects.remove(obj)
        # 次のパーツが同じ名前の画像を使えるように（残っていると .001 が付く）
        for img in list(bpy.data.images):
            bpy.data.images.remove(img)
        for mat in list(bpy.data.materials):
            bpy.data.materials.remove(mat)


main()
