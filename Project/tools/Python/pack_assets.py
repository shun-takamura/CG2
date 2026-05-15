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

# DirectStorage / D3D12 placed-footprint 制約
D3D12_TEXTURE_DATA_PITCH_ALIGNMENT = 256      # 行のバイト数の倍数
D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT = 512  # subresource 開始位置の倍数

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
# DDS → D3D12 placed-footprint レイアウト変換
# ============================================================
# DirectStorage の DSTORAGE_REQUEST_DESTINATION_MULTIPLE_SUBRESOURCES は
# source data が「各 subresource は 512B 整列開始、行は 256B 整列」という
# D3D12 placed-footprint 形式である前提。DDS はタイトパックなのでそのままでは
# 動かない。pack 時にあらかじめパディングを入れておく。

# BC ブロック 1 個あたりのバイト数 (DXGI_FORMAT 値)
_BC_BYTES_PER_BLOCK = {
    # BC1
    70: 8, 71: 8, 72: 8,
    # BC4
    79: 8, 80: 8, 81: 8,
    # BC2
    73: 16, 74: 16, 75: 16,
    # BC3
    76: 16, 77: 16, 78: 16,
    # BC5
    82: 16, 83: 16, 84: 16,
    # BC6H
    94: 16, 95: 16, 96: 16,
    # BC7
    97: 16, 98: 16, 99: 16,
}


def _is_bc_format(fmt: int) -> bool:
    return fmt in _BC_BYTES_PER_BLOCK


def _bits_per_pixel(fmt: int) -> int:
    """非圧縮フォーマットの bits/pixel。未対応は 0 を返す。"""
    table = {
        28: 32,  # R8G8B8A8_UNORM
        29: 32,  # R8G8B8A8_UNORM_SRGB
        87: 32,  # B8G8R8A8_UNORM
        91: 32,  # B8G8R8A8_UNORM_SRGB
        10: 64,  # R16G16B16A16_FLOAT
        2:  128, # R32G32B32A32_FLOAT
        62: 8,   # R8_UNORM
        49: 16,  # R8G8_UNORM
    }
    return table.get(fmt, 0)


def _parse_dds_header(dds: bytes) -> dict | None:
    """DDS ヘッダーを解釈して width/height/mips/arraySize/dxgiFormat を返す。
    DXT10 拡張ヘッダー前提。FourCC で BC1-BC5 が直接書かれている古い DDS は未対応。"""
    if len(dds) < 148 or dds[:4] != b"DDS ":
        return None
    height = struct.unpack_from("<I", dds, 12)[0]
    width  = struct.unpack_from("<I", dds, 16)[0]
    mip_count = struct.unpack_from("<I", dds, 28)[0]
    if mip_count == 0:
        mip_count = 1
    fourcc = dds[84:88]
    if fourcc != b"DX10":
        # 古い形式は未対応 (Cooker は texconv -dx10 相当の DXT10 拡張を出すので通常通らない)
        return None
    dxgi_format = struct.unpack_from("<I", dds, 128)[0]
    misc_flag   = struct.unpack_from("<I", dds, 128 + 8)[0]
    array_size  = struct.unpack_from("<I", dds, 128 + 12)[0]
    if array_size == 0:
        array_size = 1
    is_cube = (misc_flag & 0x4) != 0   # DDS_RESOURCE_MISC_TEXTURECUBE
    effective_array = array_size * (6 if is_cube else 1)
    return {
        "header_size": 148,
        "width": width,
        "height": height,
        "mip_count": mip_count,
        "array_size": effective_array,
        "dxgi_format": dxgi_format,
    }


def transform_dds_to_placed_footprint(dds: bytes) -> bytes:
    """DDS の payload を D3D12 placed-footprint レイアウトにパディングして返す。
    変換できないフォーマット (非対応) は元データをそのまま返す。"""
    info = _parse_dds_header(dds)
    if info is None:
        return dds

    fmt = info["dxgi_format"]
    is_bc = _is_bc_format(fmt)
    if is_bc:
        bytes_per_block = _BC_BYTES_PER_BLOCK[fmt]
    else:
        bpp = _bits_per_pixel(fmt)
        if bpp == 0:
            return dds  # 未対応フォーマットはそのまま
        if bpp % 8 != 0:
            return dds
        bytes_per_pixel = bpp // 8

    header_size = info["header_size"]
    src = dds[header_size:]
    src_cursor = 0

    out = bytearray()
    for _ in range(info["array_size"]):
        for m in range(info["mip_count"]):
            w = max(1, info["width"]  >> m)
            h = max(1, info["height"] >> m)
            if is_bc:
                bw = max(1, (w + 3) // 4)
                bh = max(1, (h + 3) // 4)
                src_row_pitch = bw * bytes_per_block
                num_rows = bh
            else:
                src_row_pitch = w * bytes_per_pixel
                num_rows = h
            dst_row_pitch = align_up(src_row_pitch, D3D12_TEXTURE_DATA_PITCH_ALIGNMENT)
            row_pad = dst_row_pitch - src_row_pitch
            for _row in range(num_rows):
                row_data = src[src_cursor:src_cursor + src_row_pitch]
                if len(row_data) != src_row_pitch:
                    # DDS が想定より短い → 異常。元データを返してフォールバック。
                    return dds
                src_cursor += src_row_pitch
                out.extend(row_data)
                if row_pad:
                    out.extend(b"\x00" * row_pad)
            sub_end = len(out)
            sub_aligned = align_up(sub_end, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT)
            if sub_aligned > sub_end:
                out.extend(b"\x00" * (sub_aligned - sub_end))

    return dds[:header_size] + bytes(out)


# ============================================================
# パッキング本体
# ============================================================
def collect_entries(resources_root: Path) -> list[dict]:
    """Resources/ 配下のファイルを集めて (relative_path, bytes, asset_type) のリストを返す

    Shaders/ 配下は除外する（実行時 DXC が個別ファイルとして読むため pack に含めない）。
    """
    if not resources_root.exists():
        return []

    # プロジェクトルートからの相対パス（"Resources/..." 形式）に統一
    # AssetLocator::Open の引数と一致させる
    project_root = resources_root.parent
    shaders_dir = resources_root / "Shaders"

    entries = []
    for f in sorted(resources_root.rglob("*")):
        if not f.is_file():
            continue
        # Resources/Shaders/ 配下は pack に含めない
        try:
            f.relative_to(shaders_dir)
            continue  # Shaders/ 配下なのでスキップ
        except ValueError:
            pass
        rel = f.relative_to(project_root).as_posix()
        data = f.read_bytes()
        asset_type = asset_type_from_path(rel)
        # DDS は DirectStorage MULTIPLE_SUBRESOURCES に直投げできるよう
        # D3D12 placed-footprint レイアウトに変換しておく
        if asset_type == ASSET_TEXTURE:
            data = transform_dds_to_placed_footprint(data)
        entries.append({
            "path": rel,
            "data": data,
            "asset_type": asset_type,
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
    parser.add_argument("--output", default="../Generated/Assets.pack",
                        help="出力 .pack パス（既定: ../Generated/Assets.pack = repo ルート側）")
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
