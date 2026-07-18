"""Blender が書き出した「レイアウト glTF」を、エンジンのシーン JSON へ変換する。

このスクリプトは Blender の外で動く純粋な変換器(bpy 非依存)なので、単体でテストできる。
頂点/バッファは一切読まない。見るのは nodes の transform と extras だけ。

  Blender シーン ──(アドオンが export_extras=True で書き出し)──> レイアウト glTF
                                                                      │
                                                          このスクリプト
                                                                      ↓
                                        Resources/Json/Scenes/<Scene>.json（エンジンが読む）

## 座標系（実測で確認済み。理屈だけで決めていない）

Blender(Z-up RH) --Blender の glTF エクスポータ--> glTF(Y-up RH) --ここ--> エンジン(Y-up LH)

  - Blender の Y-up 変換:  gltf = (bx, bz, -by)     ※実測: Blender(1,2,3) → glTF(1,3,-2)
  - glTF → エンジン(RH→LH): X を反転               ※cook_assets.py の `px = -px` と同じ規則

  ノードの transform は「点」ではなく「基底の変換」なので、単に X を反転するのではなく
  鏡映 S=diag(-1,1,1) による共役 M_engine = S·M_gltf·S を掛ける。
  平行移動は結果的に x だけ反転し、回転クォータニオンは (x,-y,-z,w) になる。

## extras（Blender のカスタムプロパティ）で受け取る情報

  engine_model_dir / engine_model_file : 既にクック済みの .mesh を指す → "Object3D" エントリ
  engine_tag                           : EntityTag 名（既定 "None"）
  engine_prefab                        : プレハブ名 → "Prefab" エントリ（フェーズB）
  engine_spline_tag                    : スプライン種別 → "Spline" エントリ
  engine_spline_points                 : スプラインの制御点(JSON文字列)
        ※glTF はカーブを表現できず、Blender はカーブの制御点を書き出さない(実測で確認)。
          そのためアドオン側が点列を文字列プロパティへ焼いて渡す。

使い方:
    cd Project/
    python tools/BlenderPipeline/layout_to_scene_json.py \
        --layout Assets/Scenes/Stage1_layout.gltf \
        --out Resources/Json/Scenes/StagePlay.json
"""

from __future__ import annotations

import argparse
import json
import math
import shutil
import sys
from pathlib import Path

# cook_assets.py の行列ヘルパーを再利用する（同じ規約・同じ実装を二重に書かない）。
# cook_assets.py は `if __name__ == "__main__"` ガード済みなので import しても副作用はない。
sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "Python"))
from cook_assets import _mat_from_node, _mat_mul  # noqa: E402

# 行列は「行優先で格納された列ベクトル規約」(cook_assets と同じ)。
# 平行移動は m[i][3]、点の変換は M·p。
_IDENTITY = (
    (1.0, 0.0, 0.0, 0.0),
    (0.0, 1.0, 0.0, 0.0),
    (0.0, 0.0, 1.0, 0.0),
    (0.0, 0.0, 0.0, 1.0),
)

# RH → LH の鏡映(X 反転)。cook_assets.py の頂点側 `px = -px` と対になる。
_MIRROR_X = (
    (-1.0, 0.0, 0.0, 0.0),
    (0.0, 1.0, 0.0, 0.0),
    (0.0, 0.0, 1.0, 0.0),
    (0.0, 0.0, 0.0, 1.0),
)

_EPS = 1e-6


def gltf_matrix_to_engine(m):
    """glTF(Y-up RH) のノード行列を エンジン(Y-up LH) の行列へ。M_engine = S·M·S"""
    return _mat_mul(_mat_mul(_MIRROR_X, m), _MIRROR_X)


def decompose(m) -> tuple[tuple, tuple, tuple]:
    """アフィン行列を (scale, euler, translate) へ分解する。

    euler は **MakeAffineMatrix(Transform)** が再現できる形で返す。
    ここは事故りやすいので注意: エンジンには回転合成順の違う関数が 2 つある。
        MakeRotateMatrix(Vector3) … Rz·Ry·Rx（行ベクトル規約）
        MakeAffineMatrix(Transform) … Multiply(Rx, Multiply(Ry, Rz)) = Rx·Ry·Rz（行ベクトル規約）
    Object3DInstance::Update が使うのは **MakeAffineMatrix** の方なので、そちらに合わせる。

    行ベクトルの M = S·(Rx·Ry·Rz)·T を転置して列ベクトル規約にすると
    M_col = T·(Rz·Ry·Rx)·S。その回転部 R = Rz(c)·Ry(b)·Rx(a) の閉じた式から逆算する:

        R[0][0]=cc*cb   R[1][0]=sc*cb   R[2][0]=-sb
        R[2][1]=cb*sa   R[2][2]=cb*ca
    """
    translate = (m[0][3], m[1][3], m[2][3])

    # 上 3x3 の各「列」が R の列 × scale
    cols = [(m[0][j], m[1][j], m[2][j]) for j in range(3)]
    scale = [math.sqrt(c[0] ** 2 + c[1] ** 2 + c[2] ** 2) for c in cols]

    # 鏡映(det<0)なら X スケールに符号を寄せて R を純粋な回転に保つ
    det = (
        m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1])
        - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0])
        + m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0])
    )
    if det < 0.0:
        scale[0] = -scale[0]

    r = [[0.0] * 3 for _ in range(3)]
    for j in range(3):
        s = scale[j]
        if abs(s) < _EPS:
            r[0][j], r[1][j], r[2][j] = (1.0 if j == 0 else 0.0,
                                         1.0 if j == 1 else 0.0,
                                         1.0 if j == 2 else 0.0)
            continue
        r[0][j], r[1][j], r[2][j] = cols[j][0] / s, cols[j][1] / s, cols[j][2] / s

    sb = max(-1.0, min(1.0, -r[2][0]))
    b = math.asin(sb)
    if abs(math.cos(b)) > 1e-5:
        c = math.atan2(r[1][0], r[0][0])
        a = math.atan2(r[2][1], r[2][2])
    else:
        # ジンバルロック(b=±90°): a を 0 に固定して c へ寄せる
        a = 0.0
        c = math.atan2(-r[0][1], r[1][1])

    return tuple(scale), (a, b, c), translate


def _vec3(v) -> list:
    return [float(v[0]), float(v[1]), float(v[2])]


def _transform_json(scale, euler, translate) -> dict:
    return {"scale": _vec3(scale), "rotate": _vec3(euler), "translate": _vec3(translate)}


def _mirror_point(p) -> list:
    """glTF 空間の点をエンジン空間へ（X 反転）。スプラインの制御点用。"""
    return [-float(p[0]), float(p[1]), float(p[2])]


def collect_nodes(gltf: dict) -> list:
    """シーングラフを歩いて (node, world_matrix) を集める。"""
    nodes = gltf.get("nodes", [])
    out: list = []

    def walk(idx: int, parent):
        if idx < 0 or idx >= len(nodes):
            return
        node = nodes[idx]
        world = _mat_mul(parent, _mat_from_node(node))
        out.append((node, world))
        for child in node.get("children", []):
            walk(child, world)

    scenes = gltf.get("scenes", [])
    scene_idx = gltf.get("scene", 0)
    roots = []
    if 0 <= scene_idx < len(scenes):
        roots = scenes[scene_idx].get("nodes", [])
    if roots:
        for r in roots:
            walk(r, _IDENTITY)
    else:
        # シーングラフが無い場合は全ノードをフラットに扱う
        for i in range(len(nodes)):
            out.append((nodes[i], _mat_from_node(nodes[i])))
    return out


def node_to_entry(node: dict, world) -> dict | None:
    """1 ノード → シーン JSON の 1 エントリ。対象外なら None。"""
    extras = node.get("extras") or {}
    if not isinstance(extras, dict):
        return None

    name = node.get("name", "Unnamed")
    tag = str(extras.get("engine_tag", "None"))

    # --- Spline（制御点は extras の JSON 文字列で受け取る）---
    spline_tag = extras.get("engine_spline_tag")
    if spline_tag:
        raw = extras.get("engine_spline_points")
        if not raw:
            print(f"[warn] {name}: engine_spline_tag があるが engine_spline_points が無いのでスキップ")
            return None
        try:
            pts_world_gltf = json.loads(raw) if isinstance(raw, str) else raw
        except json.JSONDecodeError as e:
            print(f"[warn] {name}: engine_spline_points が壊れているのでスキップ ({e})")
            return None
        # 制御点はアドオン側が「glTF ワールド空間」へ変換済みで渡す約束（下記の理由）。
        #   ・glTF はカーブを表現できず、Blender はカーブノードの transform すら
        #     書き出さないことがある（実測: 原点のカーブは translation キー無しで出た）。
        #     → ノードのワールド行列に頼れないので、アドオン側で世界座標へ焼いてもらう。
        #   ・ここでは他ノードと同じ RH→LH（X 反転）だけを掛ければよい＝規則が一本化される。
        pts = [_mirror_point(p) for p in pts_world_gltf]
        return {"type": "Spline", "name": name, "tag": str(spline_tag), "points": pts}

    scale, euler, translate = decompose(gltf_matrix_to_engine(world))

    # --- Prefab（フェーズB。C++ 側に分岐が入るまではエンジンが黙って無視する）---
    prefab = extras.get("engine_prefab")
    if prefab:
        return {
            "type": "Prefab",
            "name": name,
            "prefab": str(prefab),
            "transform": _transform_json(scale, euler, translate),
        }

    # --- Object3D（クック済み .mesh を指すノード）---
    model_dir = extras.get("engine_model_dir")
    model_file = extras.get("engine_model_file")
    if model_dir and model_file:
        return {
            "type": "Object3D",
            "name": name,
            "tag": tag,
            "dir": str(model_dir),
            "file": str(model_file),
            "transform": _transform_json(scale, euler, translate),
        }

    return None


def convert(layout_path: Path, scene_name: str) -> dict:
    with layout_path.open("r", encoding="utf-8") as f:
        gltf = json.load(f)

    entries = []
    skipped = 0
    for node, world in collect_nodes(gltf):
        entry = node_to_entry(node, world)
        if entry is None:
            skipped += 1
            continue
        entries.append(entry)

    print(f"[layout] {len(entries)} entries converted ({skipped} nodes skipped: engine_* プロパティ無し)")
    for t in ("Object3D", "Prefab", "Spline"):
        n = sum(1 for e in entries if e["type"] == t)
        if n:
            note = "  ※フェーズBのC++対応が入るまでエンジンは無視します" if t == "Prefab" else ""
            print(f"         {t:10s} x{n}{note}")
    return {"scene": scene_name, "objects": entries}


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--layout", required=True, help="Blender が書き出したレイアウト glTF")
    ap.add_argument("--out", required=True, help="出力するシーン JSON (Resources/Json/Scenes/*.json)")
    ap.add_argument("--scene-name", default="StagePlayScene", help='シーン JSON の "scene" フィールド')
    args = ap.parse_args()

    layout_path = Path(args.layout)
    if not layout_path.exists():
        raise SystemExit(f"layout glTF が見つかりません: {layout_path}")

    root = convert(layout_path, args.scene_name)

    out_path = Path(args.out)
    out_path.parent.mkdir(parents=True, exist_ok=True)

    # このスクリプトはシーン JSON を「まるごと置き換える」。Blender 側に無いものは消える。
    # 既定の出力先はエンジンが実際に読む StagePlay.json なので、取り返しがつくよう
    # 上書き前に必ず .bak を残す（Import してから編集するのが本来の手順だが、
    # 手順を飛ばして Export しても直前の状態には戻せるようにしておく）。
    if out_path.exists():
        backup = out_path.with_suffix(out_path.suffix + ".bak")
        shutil.copy2(out_path, backup)
        old_count = 0
        try:
            with out_path.open("r", encoding="utf-8") as f:
                old_count = len(json.load(f).get("objects", []))
        except (OSError, json.JSONDecodeError):
            pass
        new_count = len(root["objects"])
        print(f"[layout] backup -> {backup.name}  (既存 {old_count} 件 → 新規 {new_count} 件で置き換え)")
        if old_count > new_count:
            print(f"[layout] !! 注意: エントリが {old_count - new_count} 件減ります。"
                  f"Blender 側に無いものは消えます。意図しない場合は {backup.name} から戻してください")

    with out_path.open("w", encoding="utf-8") as f:
        json.dump(root, f, indent=2, ensure_ascii=False)
    print(f"[layout] wrote {out_path}")


if __name__ == "__main__":
    main()
