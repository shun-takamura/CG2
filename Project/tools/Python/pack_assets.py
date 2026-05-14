"""Resources/ 配下を 1 個の .pack に集約するパッカー（無圧縮版）。

使い方:
    cd Project/
    python tools/Python/pack_assets.py
        → Generated/Assets.pack に出力

設計:
    - Resources/ 配下の全ファイルを走査
    - パスは "Resources/Textures/uvChecker.dds" の形式で .pack 内に登録
    - エンジン側 AssetLocator::Open("Resources/...") の引数と一致する形にする
    - 圧縮タイプは現状すべて NONE（Phase B.3 で GDeflate を追加予定）

フォーマット詳細は memory の directstorage_pipeline_plan.md を参照。
"""

from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path


# ============================================================
# フォーマット定数
# ============================================================
PACK_MAGIC = 0x4B434150            # "PACK" little-endian
PACK_VERSION = 1
PACK_HEADER_SIZE = 16
PACK_INDEX_ENTRY_SIZE = 40
PACK_PAYLOAD_ALIGN = 4096          # DirectStorage 推奨

# compression_type
COMP_NONE = 0
COMP_GDEFLATE = 1
COMP_ZSTD = 2

# asset_type
ASSET_OTHER = 0
ASSET_MESH = 1
ASSET_SKEL = 2
ASSET_MAT = 3
ASSET_ANIM = 4
ASSET_TEXTURE = 5

_EXT_TO_TYPE = {
    ".mesh": ASSET_MESH,
    ".skel": ASSET_SKEL,
    ".mat":  ASSET_MAT,
    ".anim": ASSET_ANIM,
    ".dds":  ASSET_TEXTURE,
}


# ============================================================
# パス → ハッシュ
# ============================================================
def fnv1a_64(s: str) -> int:
    """FNV-1a 64bit hash (固定オフセット, RFC ベース)"""
    h = 0xcbf29ce484222325
    for b in s.encode("utf-8"):
        h ^= b
        h = (h * 0x100000001b3) & 0xFFFFFFFFFFFFFFFF
    return h


def asset_type_from_path(path: str) -> int:
    ext = Path(path).suffix.lower()
    return _EXT_TO_TYPE.get(ext, ASSET_OTHER)


# ============================================================
# パッキング本体
# ============================================================
def collect_entries(resources_root: Path) -> list[dict]:
    """Resources/ 配下のファイルを集めて (relative_path, bytes, asset_type) のリストを返す"""
    if not resources_root.exists():
        return []

    # プロジェクトルートからの相対パス（"Resources/..." 形式）に統一
    # AssetLocator::Open の引数と一致させる
    project_root = resources_root.parent

    entries = []
    for f in sorted(resources_root.rglob("*")):
        if not f.is_file():
            continue
        rel = f.relative_to(project_root).as_posix()
        data = f.read_bytes()
        entries.append({
            "path": rel,
            "data": data,
            "asset_type": asset_type_from_path(rel),
        })
    return entries


def align_up(value: int, alignment: int) -> int:
    return (value + alignment - 1) & ~(alignment - 1)


def write_pack(entries: list[dict], output_path: Path) -> None:
    # ---- ハッシュ昇順にソート（二分探索を可能にする）----
    for e in entries:
        e["name_hash"] = fnv1a_64(e["path"])
    entries.sort(key=lambda e: e["name_hash"])

    # ---- 文字列テーブル組み立て ----
    string_table = bytearray()
    for e in entries:
        e["path_offset"] = len(string_table)
        path_bytes = e["path"].encode("utf-8")
        e["path_length"] = len(path_bytes)
        string_table.extend(path_bytes)

    # ---- レイアウト計算 ----
    asset_count = len(entries)
    index_offset = PACK_HEADER_SIZE
    string_table_offset = index_offset + asset_count * PACK_INDEX_ENTRY_SIZE
    payload_section_start = align_up(string_table_offset + len(string_table),
                                     PACK_PAYLOAD_ALIGN)

    # 各エントリの payload_offset を計算（各ペイロードも 4KB 境界に揃える）
    cursor = payload_section_start
    for e in entries:
        e["payload_offset"] = cursor
        e["compressed_size"] = len(e["data"])
        e["uncompressed_size"] = len(e["data"])
        cursor = align_up(cursor + len(e["data"]), PACK_PAYLOAD_ALIGN)
    total_size = cursor

    # ---- 書き出し ----
    output_path.parent.mkdir(parents=True, exist_ok=True)
    with output_path.open("wb") as f:
        # Header (16B)
        f.write(struct.pack("<IIII",
                            PACK_MAGIC, PACK_VERSION, asset_count, index_offset))

        # Index entries (40B × N)
        for e in entries:
            f.write(struct.pack("<QIHBBQQQ",
                                e["name_hash"],
                                e["path_offset"],
                                e["path_length"],
                                COMP_NONE,
                                e["asset_type"],
                                e["compressed_size"],
                                e["uncompressed_size"],
                                e["payload_offset"]))

        # String table
        f.write(string_table)

        # Payload セクション開始までゼロ埋め
        pad = payload_section_start - f.tell()
        if pad > 0:
            f.write(b"\x00" * pad)

        # 各 payload を payload_offset へ書き込み（必要なら zero pad）
        for e in entries:
            assert f.tell() == e["payload_offset"]
            f.write(e["data"])
            # 次のエントリの境界まで zero pad
            cur = f.tell()
            next_aligned = align_up(cur, PACK_PAYLOAD_ALIGN)
            if next_aligned > cur:
                f.write(b"\x00" * (next_aligned - cur))

    # 統計
    type_names = {
        ASSET_MESH: "mesh", ASSET_SKEL: "skel", ASSET_MAT: "mat",
        ASSET_ANIM: "anim", ASSET_TEXTURE: "dds", ASSET_OTHER: "other",
    }
    type_counts: dict[int, int] = {}
    for e in entries:
        type_counts[e["asset_type"]] = type_counts.get(e["asset_type"], 0) + 1

    print(f"Packed {asset_count} assets → {output_path}")
    print(f"  Total size: {total_size:,} bytes ({total_size / 1024 / 1024:.2f} MB)")
    for t, c in sorted(type_counts.items()):
        print(f"  {type_names[t]:8s}  {c}")


# ============================================================
# メイン
# ============================================================
def main() -> int:
    parser = argparse.ArgumentParser(description="Resources/ → .pack")
    parser.add_argument("--resources", default="Resources",
                        help="入力 Resources ディレクトリ（既定: Resources）")
    parser.add_argument("--output", default="Generated/Assets.pack",
                        help="出力 .pack パス（既定: Generated/Assets.pack）")
    args = parser.parse_args()

    resources = Path(args.resources)
    output = Path(args.output)

    if not resources.exists():
        print(f"[ERROR] {resources} が存在しません。Project/ ディレクトリで実行してください。",
              file=sys.stderr)
        return 1

    entries = collect_entries(resources)
    if not entries:
        print("[ERROR] no assets to pack", file=sys.stderr)
        return 1

    write_pack(entries, output)
    return 0


if __name__ == "__main__":
    sys.exit(main())
