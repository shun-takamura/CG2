"""
タイトルロゴを貼る板ポリ（glTF）を生成する。

  python tools/Python/gen_title_logo_plane.py
  python tools/Python/cook_assets.py        # → Resources/Models/TitleLogo/logo_plane.mesh

- 板の表はエンジン空間の -Z。カメラと同じ回転（pitch, yaw）を与えると常にカメラに正対する。
- 大きさは幅 2 × 高さ 1（TitleRogo.png が 512×256 の 2:1）。原点は板の中心。
- テクスチャは glTF に持たせない（cook は glTF と同じフォルダからテクスチャを探すため）。
  TitleScene がロード後に Resources/Textures/Title/TitleRogo.dds を差し替える。

cook_assets.py の変換（X 反転・三角形の向きの反転・v = 1 - v）を見越して、
エンジン上の頂点と UV から glTF の値を逆算して書く。
"""
import json
import struct
from pathlib import Path

PROJECT_DIR = Path(__file__).resolve().parents[2]
OUT_DIR = PROJECT_DIR / "Assets" / "Models" / "TitleLogo"
NAME = "logo_plane"

WIDTH = 2.0
HEIGHT = 1.0


def main() -> None:
    hw, hh = WIDTH * 0.5, HEIGHT * 0.5
    # エンジン空間（LH、+Y 上、カメラは +Z を見る＝画面右が +X）での頂点と UV（左上が (0,0)）
    engine_verts = [
        ((-hw, +hh, 0.0), (0.0, 0.0)),  # 0: 左上
        ((+hw, +hh, 0.0), (1.0, 0.0)),  # 1: 右上
        ((-hw, -hh, 0.0), (0.0, 1.0)),  # 2: 左下
        ((+hw, -hh, 0.0), (1.0, 1.0)),  # 3: 右下
    ]
    # glTF へ逆変換：x を反転、v を反転。法線は -Z（X 反転の影響を受けない）
    positions = [(-p[0], p[1], p[2]) for p, _ in engine_verts]
    uvs = [(uv[0], 1.0 - uv[1]) for _, uv in engine_verts]
    normals = [(0.0, 0.0, -1.0)] * 4
    # glTF（RH）で -Z 側から見て反時計回り。cook が X 反転と同時に向きも反転するので表は保たれる
    indices = [0, 2, 3, 0, 3, 1]

    blob = bytearray()
    views = []

    def add_view(data: bytes, target: int) -> int:
        while len(blob) % 4:
            blob.append(0)
        views.append({"buffer": 0, "byteOffset": len(blob), "byteLength": len(data), "target": target})
        blob.extend(data)
        return len(views) - 1

    pos_view = add_view(b"".join(struct.pack("<3f", *p) for p in positions), 34962)
    nrm_view = add_view(b"".join(struct.pack("<3f", *n) for n in normals), 34962)
    uv_view = add_view(b"".join(struct.pack("<2f", *t) for t in uvs), 34962)
    idx_view = add_view(struct.pack(f"<{len(indices)}H", *indices), 34963)

    xs = [p[0] for p in positions]
    ys = [p[1] for p in positions]
    accessors = [
        {"bufferView": pos_view, "componentType": 5126, "count": 4, "type": "VEC3",
         "min": [min(xs), min(ys), 0.0], "max": [max(xs), max(ys), 0.0]},
        {"bufferView": nrm_view, "componentType": 5126, "count": 4, "type": "VEC3"},
        {"bufferView": uv_view, "componentType": 5126, "count": 4, "type": "VEC2"},
        {"bufferView": idx_view, "componentType": 5123, "count": len(indices), "type": "SCALAR"},
    ]

    gltf = {
        "asset": {"version": "2.0", "generator": "gen_title_logo_plane.py"},
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"mesh": 0, "name": NAME}],
        "materials": [{"name": "Logo", "pbrMetallicRoughness": {"baseColorFactor": [1, 1, 1, 1]}}],
        "meshes": [{"name": NAME, "primitives": [{
            "attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2},
            "indices": 3, "material": 0}]}],
        "accessors": accessors,
        "bufferViews": views,
        "buffers": [{"uri": f"{NAME}.bin", "byteLength": len(blob)}],
    }

    OUT_DIR.mkdir(parents=True, exist_ok=True)
    (OUT_DIR / f"{NAME}.bin").write_bytes(bytes(blob))
    (OUT_DIR / f"{NAME}.gltf").write_text(json.dumps(gltf, indent=2), encoding="utf-8")
    print(f"wrote {OUT_DIR / (NAME + '.gltf')}")


if __name__ == "__main__":
    main()
