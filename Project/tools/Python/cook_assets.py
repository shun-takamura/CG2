"""Assets/ ツリーを走査して Resources/ にミラー変換するアセットコンバータ。

使い方:
    cd Project/
    python tools/Python/cook_assets.py            # 差分のみ変換
    python tools/Python/cook_assets.py --force    # 全再変換
    python tools/Python/cook_assets.py --dry-run  # 何も書き換えず予定だけ出力

ファイル拡張子別の処理:
    .png         → Resources/同パス/*.dds (BC7, texconv)         [Step 1 で実装]
    .obj + .mtl  → Resources/同パス/*.mesh (バイナリ, pyassimp)  [Step 3 で実装]
    .gltf + .bin → Resources/同パス/*.glb (バイナリ統合)         [Step 5 で実装]
    .wav         → Resources/同パス/*.wav (コピー)               [Step 1 で実装]
    .mtl .bin    → スキップ（.obj/.gltf 変換に吸収）
    .hdr         → スキップ（convert_hdr_to_dds.py が処理）

Step 0 では走査・差分判定・統計表示のみ。変換ロジックは未実装。
"""

from __future__ import annotations

import argparse
import json
import struct
import subprocess
import sys
from dataclasses import dataclass
from enum import Enum, auto
from pathlib import Path

# ============================================================
# 設定
# ============================================================
ASSETS_DIR = Path("Assets")
RESOURCES_DIR = Path("Resources")
CACHE_FILE = Path("tools/Python/sync_cache.json")
TEXCONV = Path("externals/texconv/Texconv.exe")

# パスにこの部分文字列が含まれる PNG は線形値として圧縮する（マスク・ノーマル等）
LINEAR_TEXTURE_HINTS = ("MaskTexture",)

# ---- .mesh v2 フォーマット定数 ----
# Header: magic(4) + version(4) + flags(4) + vc(4) + ic(4) + smc(4)
#       + vo(4) + io(4) + so(4) + smo(4) + skeleton_path(256)
MESH_MAGIC = b"MESH"
MESH_VERSION = 2
MESH_HEADER_SIZE = 4 + 4 + 4 + 4 + 4 + 4 + 4 + 4 + 4 + 4 + 256  # = 288
MESH_VERTEX_SIZE = 16 + 8 + 12                                   # = 36
MESH_SKIN_VERTEX_SIZE = 16 + 16                                  # = 32 (joint_indices + weights)
MESH_SUBMESH_SIZE = 4 + 4 + 256                                  # = 264
MESH_PATH_LEN = 256
MESH_FLAG_HAS_SKINNING = 0x1

# ---- .mat フォーマット定数 ----
# v2 Header: magic(4) + version(4) + base_color_path(256) + color(16) + enable_lighting(4)
#          + shininess(4) + env_coeff(4) + use_env_map(4)
#          + metallic(4) + roughness(4) + shading_model(4)
MAT_MAGIC = b"MATL"
MAT_VERSION = 2
MAT_HEADER_SIZE = 4 + 4 + 256 + 16 + 4 + 4 + 4 + 4 + 4 + 4 + 4  # = 308

# ---- マテリアルデフォルト値（ModelInstance::CreateMaterialData と合わせる）----
MAT_DEFAULT_COLOR = (1.0, 1.0, 1.0, 1.0)
MAT_DEFAULT_ENABLE_LIGHTING = 1
MAT_DEFAULT_SHININESS = 50.0
MAT_DEFAULT_ENV_COEFF = 1.0
MAT_DEFAULT_USE_ENV_MAP = 0
MAT_DEFAULT_METALLIC = 0.0
MAT_DEFAULT_ROUGHNESS = 0.5
MAT_DEFAULT_SHADING_MODEL = 0  # 0=BlinnPhong, 1=PBR（既存の見た目を変えないよう既定は BlinnPhong）


class Action(Enum):
    """ファイルに対する処理種別"""
    CONVERT_PNG_TO_DDS = auto()
    CONVERT_OBJ_TO_MESH = auto()
    CONVERT_GLTF_TO_MESH = auto()  # 旧 GLTF_TO_GLB。.mesh + .skel + .mat + .anim を出力
    COPY = auto()
    SKIP = auto()
    UNKNOWN = auto()


@dataclass
class FileTask:
    """1ファイルあたりの処理タスク"""
    src: Path
    action: Action
    dst: Path | None


# ============================================================
# 分類
# ============================================================
def classify(src: Path) -> FileTask:
    """拡張子から処理種別を決定し、出力パスを組み立てる"""
    suffix = src.suffix.lower()
    rel = src.relative_to(ASSETS_DIR)

    if suffix == ".png":
        return FileTask(src, Action.CONVERT_PNG_TO_DDS, RESOURCES_DIR / rel.with_suffix(".dds"))
    if suffix == ".obj":
        return FileTask(src, Action.CONVERT_OBJ_TO_MESH, RESOURCES_DIR / rel.with_suffix(".mesh"))
    if suffix == ".gltf":
        # 出力代表は .mesh。実際には .mesh + .skel + .mat + .anim をまとめて吐く。
        return FileTask(src, Action.CONVERT_GLTF_TO_MESH, RESOURCES_DIR / rel.with_suffix(".mesh"))
    if suffix in (".wav", ".ttf"):
        return FileTask(src, Action.COPY, RESOURCES_DIR / rel)
    if suffix in {".mtl", ".bin", ".hdr", ".fbx"}:
        # .mtl / .bin: .obj / .gltf 変換で吸収
        # .hdr: convert_hdr_to_dds.py が処理
        # .fbx: .gltf があれば冗長なので無視（DCC オーサリング用に Assets/ に残しておく前提）
        return FileTask(src, Action.SKIP, None)
    return FileTask(src, Action.UNKNOWN, None)


# ============================================================
# 差分キャッシュ
# ============================================================
def load_cache() -> dict[str, float]:
    if not CACHE_FILE.exists():
        return {}
    try:
        return json.loads(CACHE_FILE.read_text(encoding="utf-8"))
    except (json.JSONDecodeError, OSError):
        return {}


def save_cache(cache: dict[str, float]) -> None:
    CACHE_FILE.parent.mkdir(parents=True, exist_ok=True)
    CACHE_FILE.write_text(
        json.dumps(cache, indent=2, sort_keys=True, ensure_ascii=False),
        encoding="utf-8",
    )


def needs_rebuild(task: FileTask, cache: dict[str, float], force: bool) -> bool:
    if force:
        return True
    if task.dst is None or not task.dst.exists():
        return True
    cached_mtime = cache.get(task.src.as_posix())
    return cached_mtime != task.src.stat().st_mtime


# ============================================================
# OBJ / MTL パース
# ============================================================
def _parse_mtl_for_texture(mtl_path: Path) -> str | None:
    """.mtl 内で最初に登場する map_Kd のファイル名を返す"""
    if not mtl_path.exists():
        return None
    with mtl_path.open("r", encoding="utf-8", errors="replace") as f:
        for line in f:
            tokens = line.split()
            if tokens and tokens[0] == "map_Kd":
                return tokens[1]
    return None


def _parse_obj(obj_path: Path):
    """OBJ を読み positions / normals / texcoords / 三角形面リスト / mtllib を返す"""
    positions: list[tuple[float, float, float]] = []
    normals: list[tuple[float, float, float]] = []
    texcoords: list[tuple[float, float]] = []
    triangles: list[tuple] = []  # 各要素は ((pi,ti,ni), (pi,ti,ni), (pi,ti,ni))
    mtllib: str | None = None

    with obj_path.open("r", encoding="utf-8", errors="replace") as f:
        for line in f:
            tokens = line.split()
            if not tokens:
                continue
            kw = tokens[0]
            if kw == "v":
                positions.append((float(tokens[1]), float(tokens[2]), float(tokens[3])))
            elif kw == "vn":
                normals.append((float(tokens[1]), float(tokens[2]), float(tokens[3])))
            elif kw == "vt":
                texcoords.append((float(tokens[1]), float(tokens[2])))
            elif kw == "f":
                verts = []
                for t in tokens[1:]:
                    parts = t.split("/")
                    pi = int(parts[0]) - 1
                    ti = int(parts[1]) - 1 if len(parts) > 1 and parts[1] else -1
                    ni = int(parts[2]) - 1 if len(parts) > 2 and parts[2] else -1
                    verts.append((pi, ti, ni))
                # 多角形は扇形三角形分割
                for i in range(1, len(verts) - 1):
                    triangles.append((verts[0], verts[i], verts[i + 1]))
            elif kw == "mtllib":
                mtllib = tokens[1]
    return positions, normals, texcoords, triangles, mtllib


# ============================================================
# .mesh / .mat ビルド (v2)
# ============================================================
def _resolve_resource_path(src_path: Path, new_suffix: str) -> str:
    """Assets/ 配下のソースパス → Resources/ 配下の対応パスに変換。

    例: Assets/Models/Enemy/enemy.obj + ".mesh" → "Resources/Models/Enemy/enemy.mesh"
    """
    try:
        rel = src_path.relative_to(ASSETS_DIR)
    except ValueError:
        return ""
    out = RESOURCES_DIR / rel.with_suffix(new_suffix)
    return out.as_posix()


def _pad_string(s: str, length: int) -> bytes:
    """UTF-8 エンコード + null 終端 + ゼロパディングで固定長バイト列にする"""
    b = s.encode("utf-8")[: length - 1]
    return b + b"\x00" * (length - len(b))


def _fixed_path_bytes(path_str: str) -> bytes:
    return _pad_string(path_str, MESH_PATH_LEN)


def _build_obj_mesh_buffers(obj_path: Path):
    """OBJ から頂点バッファ・インデックスバッファを構築する。

    座標系変換は ModelInstance::LoadModel と一致させる:
      - 三角形 winding を反転（v0, v1, v2 → v0, v2, v1）
      - position.x と normal.x を反転
      - texcoord.y を 1 - y に反転
    """
    positions, normals, texcoords, triangles, mtllib = _parse_obj(obj_path)

    vertex_map: dict[tuple[int, int, int], int] = {}
    vertex_buffer: list[tuple] = []
    index_buffer: list[int] = []

    for tri in triangles:
        # winding 反転: (v0, v1, v2) → (v0, v2, v1)
        for v_key in (tri[0], tri[2], tri[1]):
            if v_key in vertex_map:
                index_buffer.append(vertex_map[v_key])
                continue

            pi, ti, ni = v_key
            px, py, pz = positions[pi]
            u = v = 0.0
            if 0 <= ti < len(texcoords):
                u, v = texcoords[ti]
            nx, ny, nz = (0.0, 0.0, 0.0)
            if 0 <= ni < len(normals):
                nx, ny, nz = normals[ni]

            # RH → LH: x 反転, V 反転
            px = -px
            nx = -nx
            v = 1.0 - v

            new_index = len(vertex_buffer)
            vertex_map[v_key] = new_index
            vertex_buffer.append((px, py, pz, 1.0, u, v, nx, ny, nz))
            index_buffer.append(new_index)

    return vertex_buffer, index_buffer, mtllib


def _write_mesh_v2(out_path: Path,
                   vertex_buffer: list[tuple],
                   index_buffer: list[int],
                   submeshes: list[tuple[int, int, str]],
                   skeleton_path: str = "",
                   skin_buffer: list[tuple] | None = None) -> None:
    """共通 .mesh v2 ライター。

    submeshes: [(index_start, index_count, material_path)]
    skin_buffer: None または [(joint_idx[4], weights[4])] のリスト
    """
    out_path.parent.mkdir(parents=True, exist_ok=True)

    vertex_count = len(vertex_buffer)
    index_count = len(index_buffer)
    submesh_count = len(submeshes)
    has_skinning = skin_buffer is not None and len(skin_buffer) > 0
    flags = MESH_FLAG_HAS_SKINNING if has_skinning else 0

    # オフセット計算
    vertex_offset = MESH_HEADER_SIZE
    index_offset = vertex_offset + vertex_count * MESH_VERTEX_SIZE
    skin_offset = 0
    submesh_offset = index_offset + index_count * 4
    if has_skinning:
        skin_offset = submesh_offset
        submesh_offset = skin_offset + vertex_count * MESH_SKIN_VERTEX_SIZE

    with out_path.open("wb") as f:
        # ---- Header (288 bytes) ----
        f.write(MESH_MAGIC)
        f.write(struct.pack("<IIIIIIIII",
                            MESH_VERSION,
                            flags,
                            vertex_count,
                            index_count,
                            submesh_count,
                            vertex_offset,
                            index_offset,
                            skin_offset,
                            submesh_offset))
        f.write(_fixed_path_bytes(skeleton_path))

        # ---- Vertex Data ----
        for v in vertex_buffer:
            f.write(struct.pack("<9f", *v))

        # ---- Index Data ----
        if index_count > 0:
            f.write(struct.pack(f"<{index_count}I", *index_buffer))

        # ---- Skin Data (HAS_SKINNING のみ) ----
        if has_skinning:
            for joint_indices, weights in skin_buffer:
                f.write(struct.pack("<4I4f", *joint_indices, *weights))

        # ---- Submesh Data ----
        for (idx_start, idx_count, mat_path) in submeshes:
            f.write(struct.pack("<II", idx_start, idx_count))
            f.write(_fixed_path_bytes(mat_path))


def _write_mat_v2(out_path: Path, base_color_path: str,
                  color=MAT_DEFAULT_COLOR,
                  enable_lighting=MAT_DEFAULT_ENABLE_LIGHTING,
                  shininess=MAT_DEFAULT_SHININESS,
                  env_coeff=MAT_DEFAULT_ENV_COEFF,
                  use_env_map=MAT_DEFAULT_USE_ENV_MAP,
                  metallic=MAT_DEFAULT_METALLIC,
                  roughness=MAT_DEFAULT_ROUGHNESS,
                  shading_model=MAT_DEFAULT_SHADING_MODEL) -> None:
    out_path.parent.mkdir(parents=True, exist_ok=True)
    with out_path.open("wb") as f:
        f.write(MAT_MAGIC)
        f.write(struct.pack("<I", MAT_VERSION))
        f.write(_fixed_path_bytes(base_color_path))
        f.write(struct.pack("<4f", *color))
        f.write(struct.pack("<i", enable_lighting))
        f.write(struct.pack("<f", shininess))
        f.write(struct.pack("<f", env_coeff))
        f.write(struct.pack("<i", use_env_map))
        f.write(struct.pack("<f", metallic))
        f.write(struct.pack("<f", roughness))
        f.write(struct.pack("<i", shading_model))


# ============================================================
# .skel / .anim フォーマット定数
# ============================================================
SKEL_MAGIC = b"SKEL"
SKEL_VERSION = 1
SKEL_HEADER_SIZE = 16
SKEL_JOINT_SIZE = 64 + 4 + 64 + 12 + 16 + 12  # 172

ANIM_MAGIC = b"ANIM"
ANIM_VERSION = 1
ANIM_HEADER_SIZE = 24
ANIM_CHANNEL_SIZE = 64 + 6 * 4  # 88

# ============================================================
# glTF 読み込みヘルパー
# ============================================================
_GLTF_COMP = {
    5120: ("b", 1),  # BYTE
    5121: ("B", 1),  # UNSIGNED_BYTE
    5122: ("h", 2),  # SHORT
    5123: ("H", 2),  # UNSIGNED_SHORT
    5125: ("I", 4),  # UNSIGNED_INT
    5126: ("f", 4),  # FLOAT
}
_GLTF_TYPE_N = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4,
                "MAT2": 4, "MAT3": 9, "MAT4": 16}


def _gltf_load(gltf_path: Path):
    """glTF JSON と参照する全 .bin を読み込む"""
    with gltf_path.open("r", encoding="utf-8") as f:
        gltf = json.load(f)
    buffers = []
    for buf in gltf.get("buffers", []):
        uri = buf.get("uri", "")
        if uri.startswith("data:"):
            raise RuntimeError("base64 buffer URIs not supported")
        if uri:
            buffers.append((gltf_path.parent / uri).read_bytes())
        else:
            buffers.append(b"")
    return gltf, buffers


def _gltf_read_accessor(gltf, buffers, accessor_idx):
    """アクセサを読んで要素のリストを返す（SCALAR は値のリスト、VEC*/MAT* はタプルのリスト）"""
    acc = gltf["accessors"][accessor_idx]
    bv_idx = acc.get("bufferView")
    if bv_idx is None:
        # sparse accessor は未対応、ゼロ埋め
        return [0] * acc["count"]
    bv = gltf["bufferViews"][bv_idx]
    buffer = buffers[bv["buffer"]]
    base_ofs = bv.get("byteOffset", 0) + acc.get("byteOffset", 0)
    count = acc["count"]
    fmt_char, comp_size = _GLTF_COMP[acc["componentType"]]
    n = _GLTF_TYPE_N[acc["type"]]
    elem_size = comp_size * n
    stride = bv.get("byteStride", elem_size)
    out = []
    for i in range(count):
        ofs = base_ofs + i * stride
        vals = struct.unpack(f"<{n}{fmt_char}", buffer[ofs:ofs + elem_size])
        out.append(vals[0] if acc["type"] == "SCALAR" else vals)
    return out


# ============================================================
# RH → LH 座標変換ヘルパー (X 軸反転)
# ============================================================
def _mirror_x_translation(t):
    return (-t[0], t[1], t[2])


def _mirror_x_rotation(r):
    # quaternion (x, y, z, w) で Y/Z を反転
    return (r[0], -r[1], -r[2], r[3])


def _mirror_x_matrix4(m):
    """4x4 列優先行列に MirrorX * M * MirrorX を適用。
    成分 m[i + 4*j] = M[i][j]。
    結果: (行 0 ⊕ 列 0) の片方だけ true なら符号反転、両方 false / 両方 true なら不変。
    """
    out = list(m)
    for i in range(4):
        for j in range(4):
            if (i == 0) != (j == 0):
                out[i + 4 * j] = -out[i + 4 * j]
    return tuple(out)


# ============================================================
# glTF → メッシュ抽出
# ============================================================
def _gltf_extract_mesh(gltf, buffers, joint_index_offset: int = 0):
    """最初のメッシュ・最初のプリミティブから (vertex_buffer, index_buffer, skin_buffer) を構築

    joint_index_offset: 祖先ジョイントを .skel 先頭に追加した分、頂点の JOINTS_0
    に加算するオフセット
    """
    meshes = gltf.get("meshes", [])
    if not meshes:
        raise RuntimeError("no meshes in glTF")
    primitives = meshes[0].get("primitives", [])
    if not primitives:
        raise RuntimeError("no primitives in first mesh")
    prim = primitives[0]
    attrs = prim.get("attributes", {})

    if "POSITION" not in attrs:
        raise RuntimeError("primitive has no POSITION attribute")

    positions = _gltf_read_accessor(gltf, buffers, attrs["POSITION"])
    normals = _gltf_read_accessor(gltf, buffers, attrs["NORMAL"]) if "NORMAL" in attrs else None
    texcoords = _gltf_read_accessor(gltf, buffers, attrs["TEXCOORD_0"]) if "TEXCOORD_0" in attrs else None
    joints_attr = _gltf_read_accessor(gltf, buffers, attrs["JOINTS_0"]) if "JOINTS_0" in attrs else None
    weights_attr = _gltf_read_accessor(gltf, buffers, attrs["WEIGHTS_0"]) if "WEIGHTS_0" in attrs else None

    indices_idx = prim.get("indices")
    if indices_idx is None:
        raw_indices = list(range(len(positions)))
    else:
        raw_indices = _gltf_read_accessor(gltf, buffers, indices_idx)

    has_skinning = joints_attr is not None and weights_attr is not None

    vertex_buffer = []
    skin_buffer = []
    for i in range(len(positions)):
        px, py, pz = positions[i]
        u, v = (texcoords[i] if texcoords else (0.0, 0.0))
        nx, ny, nz = (normals[i] if normals else (0.0, 1.0, 0.0))
        # RH → LH
        px = -px
        nx = -nx
        v = 1.0 - v
        vertex_buffer.append((px, py, pz, 1.0, u, v, nx, ny, nz))

        if has_skinning:
            j = joints_attr[i]
            w = weights_attr[i]
            skin_buffer.append((tuple(int(x) + joint_index_offset for x in j),
                                tuple(float(x) for x in w)))

    # winding 反転 (a, b, c) → (a, c, b)
    index_buffer = []
    for i in range(0, len(raw_indices), 3):
        a, b, c = raw_indices[i], raw_indices[i + 1], raw_indices[i + 2]
        index_buffer.extend([a, c, b])

    return vertex_buffer, index_buffer, (skin_buffer if has_skinning else None)


# ============================================================
# glTF → スケルトン抽出
# ============================================================
def _gltf_extract_skeleton(gltf, buffers):
    """skin[0] からジョイントリストを構築する。

    skin.joints に含まれない祖先ノード（Armature 等）も .skel に含める。
    これによりシーンルートの transform（90° 回転 / 0.01 スケール等）を保持できる。

    Returns:
        (joints, joint_index_offset)
        joint_index_offset = 祖先ジョイントの個数（頂点の JOINTS_0 はこの分シフト必要）
    """
    skins = gltf.get("skins", [])
    if not skins:
        return None, 0
    skin = skins[0]
    skin_joint_nodes = skin["joints"]

    inv_bind_matrices = None
    if "inverseBindMatrices" in skin:
        inv_bind_matrices = _gltf_read_accessor(gltf, buffers, skin["inverseBindMatrices"])

    nodes = gltf.get("nodes", [])
    # ノード → 親ノードのマップ
    node_parent = {}
    for parent_idx, node in enumerate(nodes):
        for child_idx in node.get("children", []):
            node_parent[child_idx] = parent_idx

    # skin.joints に含まれない祖先ノードを集める
    skin_joint_set = set(skin_joint_nodes)
    ancestor_set: set[int] = set()
    for jn in skin_joint_nodes:
        cur = node_parent.get(jn)
        while cur is not None and cur not in skin_joint_set:
            ancestor_set.add(cur)
            cur = node_parent.get(cur)

    # 祖先をトポロジカル順（親→子）に並べる: ルートから BFS
    ancestor_list: list[int] = []
    if ancestor_set:
        # 祖先内のルート（親が祖先外にあるもの）から開始
        ancestor_roots = [a for a in ancestor_set
                          if node_parent.get(a) not in ancestor_set]
        ancestor_roots.sort()
        queue = list(ancestor_roots)
        while queue:
            cur = queue.pop(0)
            ancestor_list.append(cur)
            for child in nodes[cur].get("children", []):
                if child in ancestor_set and child not in ancestor_list:
                    queue.append(child)

    # 最終順序: 祖先 → skin.joints
    all_joint_nodes = ancestor_list + list(skin_joint_nodes)
    node_to_joint = {n: i for i, n in enumerate(all_joint_nodes)}

    joints = []
    for j_idx, n_idx in enumerate(all_joint_nodes):
        node = nodes[n_idx]
        name = node.get("name", f"joint_{j_idx}")

        parent_node = node_parent.get(n_idx)
        parent_joint = node_to_joint.get(parent_node, -1) if parent_node is not None else -1

        # 祖先ノードは skin.joints に含まれないので IBM は単位行列
        is_skin_joint = n_idx in skin_joint_set
        if is_skin_joint and inv_bind_matrices is not None:
            skin_idx = skin_joint_nodes.index(n_idx)
            ibm = inv_bind_matrices[skin_idx]
        else:
            ibm = (1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1)
        ibm_lh = _mirror_x_matrix4(ibm)

        t = tuple(node.get("translation", [0.0, 0.0, 0.0]))
        r = tuple(node.get("rotation", [0.0, 0.0, 0.0, 1.0]))
        s = tuple(node.get("scale", [1.0, 1.0, 1.0]))

        if "matrix" in node:
            print(f"  [WARN] joint '{name}' has node.matrix; decomposition not implemented")

        joints.append({
            "name": name,
            "parent_index": parent_joint,
            "inverse_bind_matrix": ibm_lh,
            "bind_t": _mirror_x_translation(t),
            "bind_r": _mirror_x_rotation(r),
            "bind_s": s,
        })
    return joints, len(ancestor_list)


# ============================================================
# glTF → アニメーション抽出
# ============================================================
def _gltf_extract_animations(gltf, buffers):
    """各アニメーションをジョイント名でグループ化された channels に整形して返す"""
    animations = gltf.get("animations", [])
    nodes = gltf.get("nodes", [])
    out = []
    for anim_idx, anim in enumerate(animations):
        channels = anim.get("channels", [])
        samplers = anim.get("samplers", [])

        joint_channels: dict[str, dict] = {}
        max_time = 0.0

        for ch in channels:
            target = ch["target"]
            node_idx = target.get("node")
            path = target["path"]
            if node_idx is None or path == "weights":
                continue  # morph target は未対応
            joint_name = nodes[node_idx].get("name", f"node_{node_idx}")

            sampler = samplers[ch["sampler"]]
            interp = sampler.get("interpolation", "LINEAR")
            if interp != "LINEAR":
                print(f"  [WARN] non-linear '{interp}' on {joint_name}/{path}; treated as LINEAR")

            times = _gltf_read_accessor(gltf, buffers, sampler["input"])
            values = _gltf_read_accessor(gltf, buffers, sampler["output"])

            if joint_name not in joint_channels:
                joint_channels[joint_name] = {"t": [], "r": [], "s": []}

            keys = []
            for time, val in zip(times, values):
                if time > max_time:
                    max_time = time
                if path == "translation":
                    keys.append((time, _mirror_x_translation(val)))
                elif path == "rotation":
                    keys.append((time, _mirror_x_rotation(val)))
                elif path == "scale":
                    keys.append((time, val))
            if path == "translation":
                joint_channels[joint_name]["t"] = keys
            elif path == "rotation":
                joint_channels[joint_name]["r"] = keys
            elif path == "scale":
                joint_channels[joint_name]["s"] = keys

        out.append({
            "name": anim.get("name", f"anim_{anim_idx}"),
            "duration": max_time,
            "channels": joint_channels,
        })
    return out


# ============================================================
# glTF → base_color テクスチャパス解決
# ============================================================
def _gltf_find_pbr_factors(gltf):
    """最初のマテリアルから metallicFactor / roughnessFactor を返す（無ければ glTF 既定の 1.0）"""
    materials = gltf.get("materials", [])
    if not materials:
        return (MAT_DEFAULT_METALLIC, MAT_DEFAULT_ROUGHNESS)
    pbr = materials[0].get("pbrMetallicRoughness", {})
    metallic = float(pbr.get("metallicFactor", 1.0))
    roughness = float(pbr.get("roughnessFactor", 1.0))
    return (metallic, roughness)


def _gltf_find_base_color_path(gltf, gltf_path: Path) -> str:
    materials = gltf.get("materials", [])
    if not materials:
        return ""
    pbr = materials[0].get("pbrMetallicRoughness", {})
    base = pbr.get("baseColorTexture", {})
    if "index" not in base:
        return ""
    texture_idx = base["index"]
    textures = gltf.get("textures", [])
    if texture_idx >= len(textures):
        return ""
    image_idx = textures[texture_idx].get("source")
    if image_idx is None:
        return ""
    images = gltf.get("images", [])
    if image_idx >= len(images):
        return ""
    uri = images[image_idx].get("uri", "")
    if not uri or uri.startswith("data:"):
        return ""
    try:
        rel = gltf_path.relative_to(ASSETS_DIR)
        tex_in_assets = rel.parent / uri
        return (RESOURCES_DIR / tex_in_assets.with_suffix(".dds")).as_posix()
    except ValueError:
        return ""


# ============================================================
# .skel / .anim ライター
# ============================================================
def _write_skel_v1(out_path: Path, joints: list[dict]) -> None:
    out_path.parent.mkdir(parents=True, exist_ok=True)
    with out_path.open("wb") as f:
        f.write(SKEL_MAGIC)
        f.write(struct.pack("<III", SKEL_VERSION, len(joints), 0))
        for j in joints:
            f.write(_pad_string(j["name"], 64))
            f.write(struct.pack("<i", j["parent_index"]))
            f.write(struct.pack("<16f", *j["inverse_bind_matrix"]))
            f.write(struct.pack("<3f", *j["bind_t"]))
            f.write(struct.pack("<4f", *j["bind_r"]))
            f.write(struct.pack("<3f", *j["bind_s"]))


def _write_anim_v1(out_path: Path, anim: dict) -> None:
    out_path.parent.mkdir(parents=True, exist_ok=True)
    items = list(anim["channels"].items())
    channels_offset = ANIM_HEADER_SIZE
    keyframes_start = channels_offset + len(items) * ANIM_CHANNEL_SIZE

    # 各チャンネルのキーフレームオフセットを事前計算
    cursor = keyframes_start
    ch_layouts = []
    for _, data in items:
        tc, rc, sc = len(data["t"]), len(data["r"]), len(data["s"])
        t_ofs = cursor; cursor += tc * 16   # TKey = time(4) + vec3(12)
        r_ofs = cursor; cursor += rc * 20   # RKey = time(4) + vec4(16)
        s_ofs = cursor; cursor += sc * 16   # SKey = time(4) + vec3(12)
        ch_layouts.append((tc, rc, sc, t_ofs, r_ofs, s_ofs))

    with out_path.open("wb") as f:
        # Header
        f.write(ANIM_MAGIC)
        f.write(struct.pack("<I", ANIM_VERSION))
        f.write(struct.pack("<f", anim["duration"]))
        f.write(struct.pack("<I", len(items)))
        f.write(struct.pack("<I", channels_offset))
        f.write(struct.pack("<I", 0))  # reserved
        # Channels
        for (name, _), (tc, rc, sc, t_ofs, r_ofs, s_ofs) in zip(items, ch_layouts):
            f.write(_pad_string(name, 64))
            f.write(struct.pack("<IIIIII", tc, rc, sc, t_ofs, r_ofs, s_ofs))
        # Keyframe data
        for (_, data) in items:
            for t, v in data["t"]:
                f.write(struct.pack("<f3f", t, *v))
            for t, v in data["r"]:
                f.write(struct.pack("<f4f", t, *v))
            for t, v in data["s"]:
                f.write(struct.pack("<f3f", t, *v))


# ============================================================
# glTF → .mesh + .skel + .mat + .anim 変換
# ============================================================
def convert_gltf_to_mesh(task: FileTask) -> bool:
    """glTF を 4 分割アセット (.mesh + .skel + .mat + .anim) に変換する"""
    assert task.dst is not None

    try:
        gltf, buffers = _gltf_load(task.src)
    except Exception as e:
        print(f"  [ERROR] gltf load failed: {e}")
        return False

    # スケルトンを先に抽出して祖先ジョイントの数（オフセット）を得る
    skel, joint_offset = _gltf_extract_skeleton(gltf, buffers)

    try:
        vb, ib, skin_buffer = _gltf_extract_mesh(gltf, buffers, joint_offset)
    except Exception as e:
        print(f"  [ERROR] mesh extraction failed: {e}")
        return False

    has_skinning = (skel is not None) and (skin_buffer is not None)

    # 出力先パス
    stem = task.src.stem
    out_dir = task.dst.parent
    mesh_path = task.dst                       # *.mesh
    skel_path = out_dir / f"{stem}.skel"
    mat_path = out_dir / f"{stem}.mat"

    # 参照用 (Resources 相対) パス
    mat_resource_path = _resolve_resource_path(task.src, ".mat")
    skel_resource_path = _resolve_resource_path(task.src, ".skel") if has_skinning else ""

    # .mat（glTF は PBR 形式なので metallic/roughness factor を読む。shading_model は既定の BlinnPhong）
    base_color_path = _gltf_find_base_color_path(gltf, task.src)
    metallic, roughness = _gltf_find_pbr_factors(gltf)
    _write_mat_v2(mat_path, base_color_path, metallic=metallic, roughness=roughness)

    # .skel
    if has_skinning:
        _write_skel_v1(skel_path, skel)

    # .mesh
    submeshes = [(0, len(ib), mat_resource_path)]
    _write_mesh_v2(mesh_path, vb, ib, submeshes,
                   skeleton_path=skel_resource_path,
                   skin_buffer=skin_buffer if has_skinning else None)

    # .anim (複数あれば stem_<name>.anim、単一なら stem.anim)
    animations = _gltf_extract_animations(gltf, buffers)
    for i, anim in enumerate(animations):
        if len(animations) == 1:
            anim_path = out_dir / f"{stem}.anim"
        else:
            safe_name = anim["name"].replace("/", "_").replace(" ", "_")
            anim_path = out_dir / f"{stem}_{safe_name}.anim"
        _write_anim_v1(anim_path, anim)

    print(f"  vc={len(vb)} ic={len(ib)} skin={has_skinning} "
          f"joints={len(skel) if skel else 0} anims={len(animations)} "
          f"tex='{base_color_path}'")
    return True


# ============================================================
# 個別の変換ロジック
# ============================================================
def convert_obj_to_mesh(task: FileTask) -> bool:
    """OBJ を .mesh v2 + .mat に変換する。

    OBJ → 単一 submesh（マルチマテリアル未対応）。
    マテリアルは同じディレクトリの .mat に書き出し、.mesh の submesh から参照する。
    """
    assert task.dst is not None
    try:
        vb, ib, mtllib = _build_obj_mesh_buffers(task.src)
    except Exception as e:
        print(f"  [ERROR] OBJ parse failed: {e}")
        return False

    if not vb:
        print(f"  [ERROR] no vertices produced from {task.src}")
        return False

    # ---- .mat の出力 ----
    # .mat の出力先は .mesh と同じディレクトリ・同じステム名
    mat_dst = task.dst.with_suffix(".mat")
    mat_resource_path = _resolve_resource_path(task.src, ".mat")

    # ベースカラーテクスチャ: .mtl の map_Kd を見て Resources/.../{stem}.dds に変換
    base_color_path = ""
    if mtllib:
        tex_name = _parse_mtl_for_texture(task.src.parent / mtllib)
        if tex_name:
            try:
                rel = task.src.relative_to(ASSETS_DIR)
                tex_in_assets = rel.parent / tex_name
                tex_in_resources = RESOURCES_DIR / tex_in_assets.with_suffix(".dds")
                base_color_path = tex_in_resources.as_posix()
            except ValueError:
                pass

    # OBJ/MTL は PBR 情報を持たないのでデフォルト（BlinnPhong）で書く
    _write_mat_v2(mat_dst, base_color_path)

    # ---- .mesh の出力 ----
    submeshes = [(0, len(ib), mat_resource_path)]
    _write_mesh_v2(task.dst, vb, ib, submeshes, skeleton_path="", skin_buffer=None)

    print(f"  vertices={len(vb)} indices={len(ib)} submeshes=1 "
          f"texture='{base_color_path}' -> {task.dst.name} + {mat_dst.name}")
    return True


def convert_png_to_dds(task: FileTask) -> bool:
    """PNG を BC7 DDS に変換する"""
    if not TEXCONV.exists():
        print(f"  [ERROR] {TEXCONV} が見つかりません")
        return False

    assert task.dst is not None
    task.dst.parent.mkdir(parents=True, exist_ok=True)

    # マスク等は線形保持、それ以外は SRGB
    is_linear = any(hint in task.src.parts for hint in LINEAR_TEXTURE_HINTS)
    fmt = "BC7_UNORM" if is_linear else "BC7_UNORM_SRGB"

    cmd = [
        str(TEXCONV),
        "-nologo",
        "-y",
        "-f", fmt,
        "-bc", "x",  # x = max quality BC compression mode
        "-o", str(task.dst.parent),
        str(task.src),
    ]
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        print(f"  [ERROR] texconv failed (exit {result.returncode})")
        if result.stdout:
            print(f"  stdout: {result.stdout.strip()}")
        if result.stderr:
            print(f"  stderr: {result.stderr.strip()}")
        return False
    return True


# ============================================================
# COPY (.wav / .ttf 等のバイナリそのまま)
# ============================================================
def copy_file(task: FileTask) -> bool:
    import shutil
    task.dst.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(task.src, task.dst)
    return True


# ============================================================
# 実行ディスパッチ
# ============================================================
def perform(task: FileTask, dry_run: bool) -> bool:
    """タスクを実行する。成功なら True。"""
    if dry_run:
        return True

    if task.action == Action.CONVERT_PNG_TO_DDS:
        return convert_png_to_dds(task)
    if task.action == Action.CONVERT_OBJ_TO_MESH:
        return convert_obj_to_mesh(task)
    if task.action == Action.CONVERT_GLTF_TO_MESH:
        return convert_gltf_to_mesh(task)
    if task.action == Action.COPY:
        return copy_file(task)

    # TODO: 残りの Action は後続 Step で実装
    print(f"  [TODO] {task.action.name} はまだ未実装")
    return False


# ============================================================
# メイン
# ============================================================
def main() -> int:
    parser = argparse.ArgumentParser(description="Assets/ → Resources/ アセットコンバータ")
    parser.add_argument("--force", action="store_true", help="差分を無視して全再変換")
    parser.add_argument("--dry-run", action="store_true", help="実際の変換は行わず予定のみ表示")
    args = parser.parse_args()

    if not ASSETS_DIR.exists():
        print(
            f"[ERROR] {ASSETS_DIR} が見つかりません。Project/ ディレクトリで実行してください。",
            file=sys.stderr,
        )
        return 1

    cache = load_cache()
    stats: dict[Action, int] = {action: 0 for action in Action}
    rebuilt = 0
    up_to_date = 0
    unknown_files: list[Path] = []

    for src in sorted(ASSETS_DIR.rglob("*")):
        if not src.is_file():
            continue
        task = classify(src)
        stats[task.action] += 1

        if task.action == Action.UNKNOWN:
            unknown_files.append(src)
            continue
        if task.action == Action.SKIP:
            continue

        if not needs_rebuild(task, cache, args.force):
            up_to_date += 1
            continue

        rel_src = task.src.relative_to(ASSETS_DIR).as_posix()
        rel_dst = task.dst.relative_to(RESOURCES_DIR).as_posix() if task.dst else "-"
        print(f"[{task.action.name}] {rel_src} -> {rel_dst}")

        if perform(task, args.dry_run) and not args.dry_run:
            cache[task.src.as_posix()] = task.src.stat().st_mtime
            rebuilt += 1

    if not args.dry_run:
        save_cache(cache)

    # ----- 統計 -----
    print()
    print("=" * 50)
    print(f"Walked:     {sum(stats.values())} files")
    for action, count in stats.items():
        if count > 0:
            print(f"  {action.name:30s} {count}")
    print(f"Rebuilt:    {rebuilt}")
    print(f"Up-to-date: {up_to_date}")
    if unknown_files:
        print()
        print("[WARN] 未対応の拡張子:")
        for p in unknown_files[:10]:
            print(f"  {p}")
        if len(unknown_files) > 10:
            print(f"  ... 他 {len(unknown_files) - 10} 件")
    return 0


if __name__ == "__main__":
    sys.exit(main())
