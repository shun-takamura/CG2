"""rig.solve() の結果を .gltf + .bin + .png で書き出す。

- ノード階層 = スケルトン、skins = ジョイント + inverseBindMatrices
- 各ボーン区間に細長い直方体（棒人間）を生成し、親ジョイントへ 100% ウェイト付け
- アニメーションは 回転=全フレーム / 並進=ルートのみ全フレーム / それ以外は 1 キー固定
  （PRISMEngine の cook_assets.py が T/R/S チャンネル欠落を想定していないため、
   全ジョイントに T/R/S を必ず出力する）
- .bin は外部ファイル、テクスチャは白 PNG の外部ファイル
  （cook_assets.py の _gltf_load が data URI / .glb 非対応のため）
"""

from __future__ import annotations

import json
import struct
import zlib
from pathlib import Path

from rig import JOINTS, v_add, v_sub, v_scale, v_cross, v_normalize, v_len

FLOAT = 5126
USHORT = 5123
UINT = 5125
ARRAY_BUFFER = 34962
ELEMENT_ARRAY_BUFFER = 34963

BONE_RADIUS = 0.03  # 棒人間の太さ (m)


# ============================================================
# 白 PNG（4x4）を stdlib だけで生成
# ============================================================
def _write_white_png(path: Path, size=4):
    def chunk(tag, data):
        c = tag + data
        return struct.pack(">I", len(data)) + c + struct.pack(">I", zlib.crc32(c))

    ihdr = struct.pack(">IIBBBBB", size, size, 8, 2, 0, 0, 0)  # RGB8
    raw = b"".join(b"\x00" + b"\xff" * (size * 3) for _ in range(size))
    png = (b"\x89PNG\r\n\x1a\n"
           + chunk(b"IHDR", ihdr)
           + chunk(b"IDAT", zlib.compress(raw))
           + chunk(b"IEND", b""))
    path.write_bytes(png)


# ============================================================
# 棒人間メッシュ生成（バインドポーズ空間）
# ============================================================
def _build_bone_boxes(joints):
    """各ボーン区間の直方体を連結した頂点/インデックス列を作る。

    Returns: positions, normals, uvs, joint_ids, weights, indices
    """
    positions, normals, uvs, joint_ids, weights, indices = [], [], [], [], [], []

    for j, jd in enumerate(joints):
        parent = jd["parent"]
        if parent < 0:
            continue
        p0 = joints[parent]["bind_p"]
        p1 = jd["bind_p"]
        d = v_sub(p1, p0)
        if v_len(d) < 1e-5:
            continue
        dn = v_normalize(d)
        # d に垂直な 2 軸
        ref = (0.0, 1.0, 0.0) if abs(dn[1]) < 0.9 else (1.0, 0.0, 0.0)
        u = v_normalize(v_cross(dn, ref))
        v = v_cross(dn, u)
        ur = v_scale(u, BONE_RADIUS)
        vr = v_scale(v, BONE_RADIUS)

        base = len(positions)
        corners_uv = [(1, 1), (1, -1), (-1, -1), (-1, 1)]
        for end in (p0, p1):
            for su, sv in corners_uv:
                c = v_add(end, v_add(v_scale(ur, su), v_scale(vr, sv)))
                positions.append(c)
                normals.append(v_normalize(v_add(v_scale(ur, su), v_scale(vr, sv))))
                uvs.append(((su + 1) * 0.5, (sv + 1) * 0.5))
                # ボーン区間は親ジョイントの回転で動く
                joint_ids.append((parent, 0, 0, 0))
                weights.append((1.0, 0.0, 0.0, 0.0))

        # 側面 4 面 + キャップ 2 面（頂点順: 0-3 = p0 側, 4-7 = p1 側）
        quads = [(0, 1, 5, 4), (1, 2, 6, 5), (2, 3, 7, 6), (3, 0, 4, 7)]
        for a, b, c, d4 in quads:
            indices.extend([base + a, base + b, base + c,
                            base + a, base + c, base + d4])
        indices.extend([base + 0, base + 2, base + 1, base + 0, base + 3, base + 2])
        indices.extend([base + 4, base + 5, base + 6, base + 4, base + 6, base + 7])

    return positions, normals, uvs, joint_ids, weights, indices


# ============================================================
# .bin バッファビルダー
# ============================================================
class _BinBuilder:
    def __init__(self):
        self.data = bytearray()
        self.buffer_views = []
        self.accessors = []

    def _align(self, n=4):
        while len(self.data) % n:
            self.data.append(0)

    def add(self, packed: bytes, target=None):
        """bufferView を追加して index を返す"""
        self._align()
        bv = {"buffer": 0, "byteOffset": len(self.data), "byteLength": len(packed)}
        if target is not None:
            bv["target"] = target
        self.data.extend(packed)
        self.buffer_views.append(bv)
        return len(self.buffer_views) - 1

    def accessor(self, bv, comp_type, count, type_, minmax=None):
        acc = {"bufferView": bv, "componentType": comp_type,
               "count": count, "type": type_}
        if minmax:
            acc["min"], acc["max"] = minmax
        self.accessors.append(acc)
        return len(self.accessors) - 1

    def vec3_accessor(self, values, target=None, with_minmax=False):
        packed = b"".join(struct.pack("<3f", *v) for v in values)
        bv = self.add(packed, target)
        mm = None
        if with_minmax:
            mn = [min(v[i] for v in values) for i in range(3)]
            mx = [max(v[i] for v in values) for i in range(3)]
            mm = (mn, mx)
        return self.accessor(bv, FLOAT, len(values), "VEC3", mm)

    def vec4f_accessor(self, values, target=None):
        packed = b"".join(struct.pack("<4f", *v) for v in values)
        return self.accessor(self.add(packed, target), FLOAT, len(values), "VEC4")

    def vec2_accessor(self, values, target=None):
        packed = b"".join(struct.pack("<2f", *v) for v in values)
        return self.accessor(self.add(packed, target), FLOAT, len(values), "VEC2")

    def joints_accessor(self, values, target=None):
        packed = b"".join(struct.pack("<4H", *v) for v in values)
        return self.accessor(self.add(packed, target), USHORT, len(values), "VEC4")

    def index_accessor(self, values):
        packed = struct.pack(f"<{len(values)}I", *values)
        bv = self.add(packed, ELEMENT_ARRAY_BUFFER)
        return self.accessor(bv, UINT, len(values), "SCALAR")

    def time_accessor(self, times):
        packed = struct.pack(f"<{len(times)}f", *times)
        bv = self.add(packed)
        return self.accessor(bv, FLOAT, len(times), "SCALAR",
                             ([min(times)], [max(times)]))

    def mat4_accessor(self, matrices):
        packed = b"".join(struct.pack("<16f", *m) for m in matrices)
        return self.accessor(self.add(packed), FLOAT, len(matrices), "MAT4")


def export_gltf(result, out_gltf_path, clip_name=None):
    """rig.solve() の結果を out_gltf_path (.gltf) に書き出す。

    同じフォルダに <stem>.bin と white.png も出力する。
    """
    out_path = Path(out_gltf_path)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    bin_name = out_path.stem + ".bin"
    png_name = "white.png"
    clip_name = clip_name or out_path.stem

    joints = result["joints"]
    n_joints = len(joints)

    b = _BinBuilder()

    # ---- メッシュ ----
    positions, normals, uvs, joint_ids, weights, indices = _build_bone_boxes(joints)
    acc_pos = b.vec3_accessor(positions, ARRAY_BUFFER, with_minmax=True)
    acc_nrm = b.vec3_accessor(normals, ARRAY_BUFFER)
    acc_uv = b.vec2_accessor(uvs, ARRAY_BUFFER)
    acc_jnt = b.joints_accessor(joint_ids, ARRAY_BUFFER)
    acc_wgt = b.vec4f_accessor(weights, ARRAY_BUFFER)
    acc_idx = b.index_accessor(indices)

    # ---- 逆バインド行列（バインド回転=単位なので translate(-p) を列優先で） ----
    ibms = []
    for jd in joints:
        px, py, pz = jd["bind_p"]
        ibms.append((1, 0, 0, 0,
                     0, 1, 0, 0,
                     0, 0, 1, 0,
                     -px, -py, -pz, 1))
    acc_ibm = b.mat4_accessor(ibms)

    # ---- アニメーション ----
    acc_time_static = b.time_accessor([0.0])
    acc_scale_static = b.vec3_accessor([(1.0, 1.0, 1.0)])

    samplers, channels = [], []
    # 同じ時刻列は 1 つのアクセサを共有する（キー削減でチャンネルごとに
    # 時刻列が変わるため、全体で 1 本の共有アクセサは使えない。
    # 未削減なら全チャンネルが同じ時刻列 = 従来どおり 1 本に集約される）
    _time_acc_cache = {}

    def time_acc(times):
        key = tuple(times)
        if key not in _time_acc_cache:
            _time_acc_cache[key] = b.time_accessor(list(times))
        return _time_acc_cache[key]

    def add_channel(node, path, input_acc, output_acc):
        samplers.append({"input": input_acc, "interpolation": "LINEAR",
                         "output": output_acc})
        channels.append({"sampler": len(samplers) - 1,
                         "target": {"node": node, "path": path}})

    for j, jd in enumerate(joints):
        # rotation: そのジョイント自身の時刻列を使う
        r_keys = result["rot_keys"][jd["name"]]
        add_channel(j, "rotation", time_acc([t for t, _ in r_keys]),
                    b.vec4f_accessor([q for _, q in r_keys]))
        # translation: ルートは全フレーム、その他はバインド値 1 キー
        if jd["parent"] < 0:
            t_keys = result["root_trans_keys"]
            add_channel(j, "translation", time_acc([t for t, _ in t_keys]),
                        b.vec3_accessor([p for _, p in t_keys]))
        else:
            add_channel(j, "translation", acc_time_static,
                        b.vec3_accessor([jd["bind_t"]]))
        # scale: 1 キー固定（アクセサは共有）
        add_channel(j, "scale", acc_time_static, acc_scale_static)

    # ---- ノード階層 ----
    nodes = []
    for j, jd in enumerate(joints):
        node = {"name": jd["name"], "translation": list(jd["bind_t"]),
                "rotation": [0.0, 0.0, 0.0, 1.0], "scale": [1.0, 1.0, 1.0]}
        children = [c for c, cd in enumerate(joints) if cd["parent"] == j]
        if children:
            node["children"] = children
        nodes.append(node)
    mesh_node = len(nodes)
    nodes.append({"name": "MocapMesh", "mesh": 0, "skin": 0})

    gltf = {
        "asset": {"version": "2.0", "generator": "MediaPipeMocap"},
        "scene": 0,
        "scenes": [{"nodes": [0, mesh_node]}],
        "nodes": nodes,
        "meshes": [{
            "name": "MocapMesh",
            "primitives": [{
                "attributes": {
                    "POSITION": acc_pos, "NORMAL": acc_nrm, "TEXCOORD_0": acc_uv,
                    "JOINTS_0": acc_jnt, "WEIGHTS_0": acc_wgt,
                },
                "indices": acc_idx,
                "material": 0,
            }],
        }],
        "skins": [{
            "name": "MocapSkin",
            "joints": list(range(n_joints)),
            "inverseBindMatrices": acc_ibm,
            "skeleton": 0,
        }],
        "materials": [{
            "name": "MocapMaterial",
            "pbrMetallicRoughness": {
                "baseColorTexture": {"index": 0},
                "metallicFactor": 0.0,
                "roughnessFactor": 0.9,
            },
            "doubleSided": True,
        }],
        "textures": [{"source": 0}],
        "images": [{"uri": png_name}],
        "animations": [{
            "name": clip_name,
            "samplers": samplers,
            "channels": channels,
        }],
        "buffers": [{"uri": bin_name, "byteLength": len(b.data)}],
        "bufferViews": b.buffer_views,
        "accessors": b.accessors,
    }

    out_path.write_text(json.dumps(gltf, indent=1), encoding="utf-8")
    (out_path.parent / bin_name).write_bytes(bytes(b.data))
    _write_white_png(out_path.parent / png_name)

    return [out_path, out_path.parent / bin_name, out_path.parent / png_name]


# ============================================================
# リターゲット済みアニメーションの統合 glTF 出力
# ============================================================
def export_retargeted_gltf(retargeted: dict, target, out_gltf_path,
                           clip_name=None) -> list:
    """ターゲットモデルのメッシュ・スキン・マテリアルをそのまま引き継ぎ、
    リターゲット済みアニメーションだけ差し替えた .gltf を書き出す。

    元のバッファ（メッシュ等）は解釈せずバイト列のままコピーし、
    アニメーションは追加の第2バッファ（<name>_anim.bin）に書き出す。
    target: gltf_import.TargetModel
    """
    import copy
    import shutil

    out_path = Path(out_gltf_path)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    clip_name = clip_name or out_path.stem
    anim_bin_name = out_path.stem + "_anim.bin"

    gltf = copy.deepcopy(target.gltf)
    gltf.pop("animations", None)
    gltf.setdefault("bufferViews", [])
    gltf.setdefault("accessors", [])

    bv_offset = len(gltf["bufferViews"])
    acc_offset = len(gltf["accessors"])
    buffer_index = len(gltf.get("buffers", []))

    # アニメーションデータを新規バッファに構築
    b = _BinBuilder()
    times = retargeted["times"]
    acc_time_static = b.time_accessor([times[0]])

    # 同じ時刻列はアクセサを共有する（キー削減でチャンネルごとに時刻列が変わる）
    _time_acc_cache = {}

    def time_acc(ts):
        key = tuple(ts)
        if key not in _time_acc_cache:
            _time_acc_cache[key] = b.time_accessor(list(ts))
        return _time_acc_cache[key]

    name_to_idx = {n: i for i, n in enumerate(retargeted["joint_names"])}
    samplers, channels = [], []

    def add_channel(node, path, input_acc, output_acc):
        samplers.append({"input": input_acc + acc_offset,
                         "interpolation": "LINEAR",
                         "output": output_acc + acc_offset})
        channels.append({"sampler": len(samplers) - 1,
                         "target": {"node": node, "path": path}})

    for name, r_keys in retargeted["rot_local_keys"].items():
        k = name_to_idx[name]
        node = target.node_indices[k]
        add_channel(node, "rotation", time_acc([t for t, _ in r_keys]),
                    b.vec4f_accessor([q for _, q in r_keys]))
        if name == retargeted["root_joint"] and retargeted["root_trans_keys"]:
            t_keys = retargeted["root_trans_keys"]
            add_channel(node, "translation", time_acc([t for t, _ in t_keys]),
                        b.vec3_accessor([p for _, p in t_keys]))
        else:
            add_channel(node, "translation", acc_time_static,
                        b.vec3_accessor([tuple(retargeted["bind_local_t"][k])]))
        add_channel(node, "scale", acc_time_static,
                    b.vec3_accessor([tuple(retargeted["bind_local_s"][k])]))

    # 新規バッファの bufferView / accessor を既存の後ろに連結
    # （builder内のローカル index は既存分だけオフセットして繋ぐ）
    for bv in b.buffer_views:
        bv["buffer"] = buffer_index
    for acc in b.accessors:
        acc["bufferView"] += bv_offset
    gltf["bufferViews"].extend(b.buffer_views)
    gltf["accessors"].extend(b.accessors)
    gltf.setdefault("buffers", []).append(
        {"uri": anim_bin_name, "byteLength": len(b.data)})
    gltf["animations"] = [{
        "name": clip_name,
        "samplers": samplers,
        "channels": channels,
    }]

    # 出力
    written = [out_path]
    out_path.write_text(json.dumps(gltf, indent=1), encoding="utf-8")
    anim_bin_path = out_path.parent / anim_bin_name
    anim_bin_path.write_bytes(bytes(b.data))
    written.append(anim_bin_path)

    # 元のバッファ・画像ファイルを出力先へコピー（同一フォルダならスキップ）
    src_dir = target.path.parent
    refs = [buf.get("uri", "") for buf in target.gltf.get("buffers", [])]
    refs += [img.get("uri", "") for img in target.gltf.get("images", [])]
    for uri in refs:
        if not uri or uri.startswith("data:"):
            continue
        src = src_dir / uri
        dst = out_path.parent / uri
        if src.exists() and src.resolve() != dst.resolve():
            dst.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy(src, dst)
            written.append(dst)

    return written
