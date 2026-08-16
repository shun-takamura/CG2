"""ターゲットモデル（.gltf + 外部 .bin）の読み込み。リターゲット機能の下敷き。

- 骨格抽出: ノード階層（名前・親・ローカルTRS）、skin[0] の joints / inverseBindMatrices
- 統合エクスポート用に glTF JSON とバッファのバイト列をそのまま保持する
  （メッシュデータは解釈せず、再出力時にバイト列のままコピーする）

対応範囲: .gltf(JSON) + 外部 .bin、単一スキン、TRSノード（matrixノードは非対応）。
.glb / data URI / 複数スキン / モーフターゲットは明示的にエラーにする。
"""

from __future__ import annotations

import json
import struct
from pathlib import Path

from rig import q_mul, Q_IDENTITY

_COMP = {
    5120: ("b", 1), 5121: ("B", 1), 5122: ("h", 2),
    5123: ("H", 2), 5125: ("I", 4), 5126: ("f", 4),
}
_TYPE_N = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4,
           "MAT2": 4, "MAT3": 9, "MAT4": 16}


class TargetModel:
    """読み込んだターゲット glTF の骨格情報 + 生データ"""

    def __init__(self, gltf: dict, buffers: list[bytes], path: Path):
        self.gltf = gltf
        self.buffers = buffers
        self.path = path

        # 骨格（skin.joints + その祖先ノードを含む全ジョイント）
        self.joint_names: list[str] = []       # 階層順（親が先）
        self.node_indices: list[int] = []      # 対応する glTF ノード index
        self.parent: list[int] = []            # このリスト内での親 index (-1=ルート)
        self.bind_local_t: list[tuple] = []
        self.bind_local_r: list[tuple] = []
        self.bind_local_s: list[tuple] = []
        self.bind_global_r: list[tuple] = []   # 階層を合成したバインド時グローバル回転
        self.skin_joint_set: set[int] = set()  # skin.joints に含まれるノード index

        self._extract_skeleton()

    # ------------------------------------------------------------
    def _extract_skeleton(self):
        gltf = self.gltf
        skins = gltf.get("skins", [])
        if not skins:
            raise ValueError("このglTFにはスキン（ボーン）がありません")
        if len(skins) > 1:
            raise ValueError(f"複数スキン({len(skins)})は非対応です")
        skin = skins[0]
        skin_joints = skin["joints"]
        self.skin_joint_set = set(skin_joints)

        nodes = gltf.get("nodes", [])
        for mesh in gltf.get("meshes", []):
            for prim in mesh.get("primitives", []):
                if "targets" in prim:
                    raise ValueError("モーフターゲット付きモデルは非対応です")

        # ノード→親ノードのマップ
        node_parent: dict[int, int] = {}
        for parent_idx, node in enumerate(nodes):
            for child in node.get("children", []):
                node_parent[child] = parent_idx

        # skin.joints に含まれない祖先（Armature等）も骨格に含める
        ancestor_set: set[int] = set()
        for jn in skin_joints:
            cur = node_parent.get(jn)
            while cur is not None and cur not in self.skin_joint_set:
                ancestor_set.add(cur)
                cur = node_parent.get(cur)

        ancestor_list: list[int] = []
        if ancestor_set:
            roots = sorted(a for a in ancestor_set
                           if node_parent.get(a) not in ancestor_set)
            queue = list(roots)
            while queue:
                cur = queue.pop(0)
                ancestor_list.append(cur)
                for child in nodes[cur].get("children", []):
                    if child in ancestor_set and child not in ancestor_list:
                        queue.append(child)

        all_nodes = ancestor_list + list(skin_joints)
        node_to_joint = {n: i for i, n in enumerate(all_nodes)}

        for n_idx in all_nodes:
            node = nodes[n_idx]
            if "matrix" in node:
                raise ValueError(
                    f"ノード '{node.get('name', n_idx)}' が matrix 表現です（TRS のみ対応）。"
                    "Blender で再エクスポートしてください")
            name = node.get("name", f"joint_{len(self.joint_names)}")
            p_node = node_parent.get(n_idx)
            p_joint = node_to_joint.get(p_node, -1) if p_node is not None else -1
            if p_joint >= len(self.joint_names):
                raise ValueError("骨格の階層順が壊れています（親が子より後）")

            t = tuple(node.get("translation", [0.0, 0.0, 0.0]))
            r = tuple(node.get("rotation", [0.0, 0.0, 0.0, 1.0]))
            s = tuple(node.get("scale", [1.0, 1.0, 1.0]))

            self.joint_names.append(name)
            self.node_indices.append(n_idx)
            self.parent.append(p_joint)
            self.bind_local_t.append(t)
            self.bind_local_r.append(r)
            self.bind_local_s.append(s)
            parent_g = self.bind_global_r[p_joint] if p_joint >= 0 else Q_IDENTITY
            self.bind_global_r.append(q_mul(parent_g, r))

    # ------------------------------------------------------------
    def bind_global_position(self, joint_idx: int) -> tuple:
        """バインド時のグローバル位置（回転・並進を階層合成。スケールは無視）"""
        from rig import v_add, q_conj

        def rotate(q, v):
            x, y, z, w = q
            qv = (v[0], v[1], v[2], 0.0)
            r = q_mul(q_mul(q, qv), q_conj(q))
            return (r[0], r[1], r[2])

        chain = []
        j = joint_idx
        while j >= 0:
            chain.append(j)
            j = self.parent[j]
        chain.reverse()

        pos = (0.0, 0.0, 0.0)
        rot = Q_IDENTITY
        for j in chain:
            pos = v_add(pos, rotate(rot, self.bind_local_t[j]))
            rot = q_mul(rot, self.bind_local_r[j])
        return pos

    def joint_index(self, name: str) -> int:
        return self.joint_names.index(name)


def load_gltf(path) -> TargetModel:
    """glTF ファイルを読み込んで TargetModel を返す"""
    p = Path(path)
    if p.suffix.lower() == ".glb":
        raise ValueError(".glb（バイナリ統合形式）は非対応です。"
                         "Blender で「glTF Separate (.gltf + .bin)」形式で"
                         "エクスポートしてください")

    gltf = json.loads(p.read_text(encoding="utf-8"))

    buffers = []
    for buf in gltf.get("buffers", []):
        uri = buf.get("uri", "")
        if uri.startswith("data:"):
            raise ValueError("data URI 埋め込みバッファは非対応です。"
                             "外部 .bin 形式でエクスポートしてください")
        if not uri:
            raise ValueError("バッファに uri がありません（.glb 由来？）")
        bin_path = p.parent / uri
        if not bin_path.exists():
            raise ValueError(f"バッファファイルが見つかりません: {bin_path}")
        buffers.append(bin_path.read_bytes())

    return TargetModel(gltf, buffers, p)


def read_accessor(gltf: dict, buffers: list[bytes], accessor_idx: int):
    """アクセサを読んで要素リストを返す（SCALARは値、VEC*/MAT*はタプル）"""
    acc = gltf["accessors"][accessor_idx]
    bv_idx = acc.get("bufferView")
    if bv_idx is None:
        return [0] * acc["count"]
    bv = gltf["bufferViews"][bv_idx]
    buffer = buffers[bv["buffer"]]
    base = bv.get("byteOffset", 0) + acc.get("byteOffset", 0)
    fmt, comp_size = _COMP[acc["componentType"]]
    n = _TYPE_N[acc["type"]]
    elem = comp_size * n
    stride = bv.get("byteStride", elem)
    out = []
    for i in range(acc["count"]):
        ofs = base + i * stride
        vals = struct.unpack(f"<{n}{fmt}", buffer[ofs:ofs + elem])
        out.append(vals[0] if acc["type"] == "SCALAR" else vals)
    return out
