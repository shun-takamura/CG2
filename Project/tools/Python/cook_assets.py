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

# ---- .mesh フォーマット定数 ----
MESH_MAGIC = b"MESH"
MESH_VERSION = 1
MESH_HEADER_SIZE = 4 + 4 + 4 + 4 + 4 + 4 + 256  # = 280
MESH_VERTEX_SIZE = 16 + 8 + 12                   # = 36 (position + texcoord + normal)
MESH_TEXTURE_PATH_LEN = 256


class Action(Enum):
    """ファイルに対する処理種別"""
    CONVERT_PNG_TO_DDS = auto()
    CONVERT_OBJ_TO_MESH = auto()
    CONVERT_GLTF_TO_GLB = auto()
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
        return FileTask(src, Action.CONVERT_GLTF_TO_GLB, RESOURCES_DIR / rel.with_suffix(".glb"))
    if suffix == ".wav":
        return FileTask(src, Action.COPY, RESOURCES_DIR / rel)
    if suffix in {".mtl", ".bin", ".hdr"}:
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
# .mesh ビルド
# ============================================================
def _build_mesh_buffers(obj_path: Path):
    """OBJ から (vertex_buffer, index_buffer, texture_path_str) を構築する。

    座標系変換は ModelInstance::LoadModel と一致させる:
      - 三角形 winding を反転（v0, v1, v2 → v0, v2, v1）
      - position.x と normal.x を反転
      - texcoord.y を 1 - y に反転
    """
    positions, normals, texcoords, triangles, mtllib = _parse_obj(obj_path)

    # テクスチャパスの解決: .mtl の map_Kd を見て、Resources/.../{stem}.dds に向ける
    texture_path_str = ""
    if mtllib:
        tex_name = _parse_mtl_for_texture(obj_path.parent / mtllib)
        if tex_name:
            try:
                rel = obj_path.relative_to(ASSETS_DIR)
                tex_in_assets = rel.parent / tex_name
                tex_in_resources = RESOURCES_DIR / tex_in_assets.with_suffix(".dds")
                texture_path_str = tex_in_resources.as_posix()
            except ValueError:
                # ASSETS_DIR 外の OBJ はスキップ
                pass

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

    return vertex_buffer, index_buffer, texture_path_str


def _write_mesh_binary(out_path: Path, vertex_buffer: list[tuple],
                      index_buffer: list[int], texture_path_str: str) -> None:
    out_path.parent.mkdir(parents=True, exist_ok=True)

    vertex_count = len(vertex_buffer)
    index_count = len(index_buffer)
    vertex_offset = MESH_HEADER_SIZE
    index_offset = MESH_HEADER_SIZE + vertex_count * MESH_VERTEX_SIZE

    tex_bytes = texture_path_str.encode("utf-8")[: MESH_TEXTURE_PATH_LEN - 1]
    tex_padded = tex_bytes + b"\x00" * (MESH_TEXTURE_PATH_LEN - len(tex_bytes))

    with out_path.open("wb") as f:
        f.write(MESH_MAGIC)
        f.write(struct.pack("<IIIII", MESH_VERSION, vertex_count, index_count,
                            vertex_offset, index_offset))
        f.write(tex_padded)
        for v in vertex_buffer:
            f.write(struct.pack("<9f", *v))
        if index_count > 0:
            f.write(struct.pack(f"<{index_count}I", *index_buffer))


# ============================================================
# GLTF + BIN → GLB 統合
# ============================================================
GLB_MAGIC = 0x46546C67   # "glTF"
GLB_VERSION = 2
GLB_CHUNK_JSON = 0x4E4F534A  # "JSON"
GLB_CHUNK_BIN = 0x004E4942   # "BIN\0"


def _pad4(data: bytes, pad_byte: int) -> bytes:
    """4 バイト境界に揃えるためにパディング"""
    rem = len(data) % 4
    if rem == 0:
        return data
    return data + bytes([pad_byte]) * (4 - rem)


def convert_gltf_to_glb(task: FileTask) -> bool:
    """.gltf + 同名 .bin → 単一 .glb に統合。テクスチャ URI は .png → .dds に書き換える。"""
    assert task.dst is not None

    try:
        with task.src.open("r", encoding="utf-8") as f:
            gltf = json.load(f)
    except (OSError, json.JSONDecodeError) as e:
        print(f"  [ERROR] gltf load failed: {e}")
        return False

    # ---- buffers: 外部 .bin を読み込んで URI を取り除く ----
    bin_blob = b""
    buffers = gltf.get("buffers", [])
    if len(buffers) > 1:
        print(f"  [ERROR] multiple buffers not supported (count={len(buffers)})")
        return False
    if buffers:
        buf = buffers[0]
        uri = buf.get("uri")
        if uri:
            bin_path = task.src.parent / uri
            if not bin_path.exists():
                print(f"  [ERROR] referenced bin not found: {bin_path}")
                return False
            bin_blob = bin_path.read_bytes()
            del buf["uri"]
            # byteLength は元の値を維持（GLB ではパディング前の値）
            buf["byteLength"] = len(bin_blob)

    # ---- images: .png → .dds に書き換え ----
    for img in gltf.get("images", []):
        uri = img.get("uri")
        if uri and uri.lower().endswith(".png"):
            img["uri"] = uri[:-4] + ".dds"

    # ---- JSON chunk バイト列 ----
    json_bytes = json.dumps(gltf, separators=(",", ":"), ensure_ascii=False).encode("utf-8")
    json_padded = _pad4(json_bytes, 0x20)  # 空白でパディング

    # ---- BIN chunk バイト列 ----
    bin_padded = _pad4(bin_blob, 0x00) if bin_blob else b""

    # ---- GLB 全体組み立て ----
    total_len = 12 + 8 + len(json_padded)
    if bin_padded:
        total_len += 8 + len(bin_padded)

    task.dst.parent.mkdir(parents=True, exist_ok=True)
    with task.dst.open("wb") as f:
        # Header
        f.write(struct.pack("<III", GLB_MAGIC, GLB_VERSION, total_len))
        # JSON chunk
        f.write(struct.pack("<II", len(json_padded), GLB_CHUNK_JSON))
        f.write(json_padded)
        # BIN chunk
        if bin_padded:
            f.write(struct.pack("<II", len(bin_padded), GLB_CHUNK_BIN))
            f.write(bin_padded)

    print(f"  json={len(json_bytes)}B bin={len(bin_blob)}B total={total_len}B")
    return True


# ============================================================
# 個別の変換ロジック
# ============================================================
def convert_obj_to_mesh(task: FileTask) -> bool:
    """OBJ を .mesh バイナリに変換"""
    assert task.dst is not None
    try:
        vb, ib, tex = _build_mesh_buffers(task.src)
    except Exception as e:
        print(f"  [ERROR] OBJ parse failed: {e}")
        return False

    if not vb:
        print(f"  [ERROR] no vertices produced from {task.src}")
        return False

    _write_mesh_binary(task.dst, vb, ib, tex)
    print(f"  vertices={len(vb)} indices={len(ib)} texture='{tex}'")
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
    if task.action == Action.CONVERT_GLTF_TO_GLB:
        return convert_gltf_to_glb(task)

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
