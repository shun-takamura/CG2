"""メイン武器（右手の装備。20_Loadout.md §1）の Tripo モデルを cook できる形にするスクリプト。

使い方（Project/ で実行）:
    blender -b --factory-startup -P tools/BlenderPipeline/prep_main_weapons.py

Assets/Models/Weapons/MainWeapons/<MiddlePower|LowPower|HightPower>/*.glb（どれもメッシュ 1・マテリアル 1・画像 3 枚。
銃が床に寝た向きで、銃身が水平面の斜めを向いている）を読み、
  - 銃身（いちばん長い向き）を水平面の主成分で求めて、銃口が Blender の −Y（エンジンの +Z）を向くように Z 軸まわりに回す。
    銃口の側は「端の高さが低い方（細い銃身）」で決め、違っていたら FLIP_MUZZLE で反転する
  - 包む箱の中心を原点へ（右手に付けるずらしは MainWeapon.json の grip で合わせる）
  - 金属度・粗さのテクスチャを係数（中央値）に置き換える（prep_shields.py と同じ）
  - 色と法線のテクスチャを PNG で書き出す
して <フォルダ>/Export/MainWeapon_<名前>.gltf に書き出す。その後 `python tools/Python/cook_assets.py` で Resources へ。

座標：cook は glTF の X だけを反転するので、エンジンでは銃口が +Z、上が +Y。
"""

import json
import math
import struct
import zlib
from pathlib import Path

import bpy
import numpy as np
from mathutils import Matrix, Vector

LOG = "[prep_main_weapons]"
ROOT = Path("Assets/Models/Weapons/MainWeapons")
WEAPONS = {
    "MiddlePower": "MainWeapon_MiddlePower.glb",
    "LowPower": "MainWeapon_LowPower.glb",
    "HightPower": "MainWeapon_HightPower.glb",
}
# 銃口の判定が逆だったものをここに足す
FLIP_MUZZLE = {"LowPower"}  # 前の太い筒の方が背の高さが低かった


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


def bounds(mesh):
    co = np.empty(len(mesh.data.vertices) * 3, dtype=np.float64)
    mesh.data.vertices.foreach_get("co", co)
    return co.reshape(-1, 3)


def import_weapon(name, file):
    before = set(bpy.data.objects)
    bpy.ops.import_scene.gltf(filepath=str(ROOT / name / file))
    new = [o for o in bpy.data.objects if o not in before]
    mesh = next(o for o in new if o.type == "MESH")
    for o in new:
        if o is not mesh:
            bpy.data.objects.remove(o)
    mesh.data.transform(mesh.matrix_world)
    mesh.matrix_world = Matrix.Identity(4)
    mesh.name = mesh.data.name = f"MainWeapon_{name}"

    # 銃身の向き＝水平面（XY）の主成分
    v = bounds(mesh)
    xy = v[:, :2] - v[:, :2].mean(axis=0)
    w, vecs = np.linalg.eigh(np.cov(xy.T))
    axis = vecs[:, np.argmax(w)]
    # 端の 12% の高さ（Z の幅）を比べ、低い方（細い銃身）を銃口にする
    t = xy @ axis
    lo_t, hi_t = t.min(), t.max()
    span = hi_t - lo_t
    end_lo = v[t < lo_t + span * 0.12, 2]
    end_hi = v[t > hi_t - span * 0.12, 2]
    h_lo = end_lo.max() - end_lo.min()
    h_hi = end_hi.max() - end_hi.min()
    muzzle = axis if h_hi < h_lo else -axis
    if name in FLIP_MUZZLE:
        muzzle = -muzzle
    # muzzle を −Y へ回す
    angle = math.atan2(-1.0, 0.0) - math.atan2(muzzle[1], muzzle[0])
    mesh.data.transform(Matrix.Rotation(angle, 4, "Z"))

    v = bounds(mesh)
    lo, hi = v.min(axis=0), v.max(axis=0)
    center = Vector(((lo + hi) * 0.5).tolist())
    mesh.data.transform(Matrix.Translation(-center))
    size = hi - lo
    log(f"{name}: rotated {math.degrees(angle):.1f} deg, end heights {h_lo:.3f}/{h_hi:.3f}, "
        f"size(x,y,z)={tuple(round(float(c), 3) for c in size)}")
    return mesh


def prepare_material(obj, name):
    mat = obj.data.materials[0]
    mat.name = f"MainWeapon_{name}"
    bsdf, base = tex_node_into(mat, "Base Color")
    _, normal = tex_node_into(mat, "Normal")
    _, mr = tex_node_into(mat, "Roughness")
    base.image.name = f"MainWeapon_{name}_BaseColor"
    normal.image.name = f"MainWeapon_{name}_NormalMap"
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
    for name, file in WEAPONS.items():
        obj = import_weapon(name, file)
        info = prepare_material(obj, name)
        out_dir = ROOT / name / "Export"
        out_dir.mkdir(parents=True, exist_ok=True)
        save_textures_as_png(info, out_dir)
        export_object(obj, out_dir / f"MainWeapon_{name}.gltf")
        bpy.data.objects.remove(obj)


main()
