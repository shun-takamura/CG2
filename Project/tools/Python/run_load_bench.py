"""テクスチャロード計測（FS / Pack+CPU / Pack+DStorage）を複数回回して中央値の比較表を出す。

exe を `--bench-textures --exit-after-bench` 付きで起動すると、起動直後に全 .dds（Resources/Bench/texture_bench.txt）を
読み込んで GPU 完了まで待った時間を Logs/load_bench.csv に 1 行追記して終了する（Framework / TextureLoadBenchmark）。
このスクリプトはそれを 3 方式 × N 回繰り返し、今回ぶんの行だけを集計する。

  - 方式の実行順は毎ラウンドずらす（OS のファイルキャッシュや実行順による偏りを減らす）
  - デバッガ無しで起動する（OutputDebugString がデバッガ接続時だけ重くなるため）
  - CPU は GetProcessTimes（約15.6ms刻み）ではなく CPU サイクル数で FS 比を出す

    py tools\\Python\\run_load_bench.py                       # Development / 5 回
    py tools\\Python\\run_load_bench.py --config Release -n 10
    py tools\\Python\\run_load_bench.py --out Documents\\load_bench_result.md

作業ディレクトリは Project/（Resources・Logs・../Generated/Assets.pack の相対パスがここ基準）。
"""
import argparse
import csv
import statistics
import subprocess
import sys
import time
from pathlib import Path

PROJECT_DIR = Path(__file__).resolve().parents[2]
CSV_PATH = PROJECT_DIR / "Logs" / "load_bench.csv"

MODES = [
    ("FS", ["--use-fs"]),
    ("Pack+CPU", ["--use-pack", "--no-dstorage"]),
    ("Pack+DStorage", ["--use-pack"]),
]
BENCH_ARGS = ["--bench-textures", "--exit-after-bench"]


def read_rows():
    if not CSV_PATH.exists():
        return []
    with CSV_PATH.open(encoding="utf-8", newline="") as f:
        return list(csv.DictReader(f))


def run_once(exe, mode_args, timeout):
    cmd = [str(exe)] + mode_args + BENCH_ARGS
    t0 = time.perf_counter()
    proc = subprocess.run(cmd, cwd=PROJECT_DIR, timeout=timeout)
    return proc.returncode, time.perf_counter() - t0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--config", default="Development", help="Generated/Output/<config>/CG2_0_1.exe を使う")
    ap.add_argument("-n", "--runs", type=int, default=5, help="1 方式あたりの回数")
    ap.add_argument("--timeout", type=float, default=120.0, help="1 回あたりのタイムアウト秒")
    ap.add_argument("--out", help="結果の Markdown を書き出すパス（省略時は標準出力のみ）")
    args = ap.parse_args()

    exe = PROJECT_DIR.parent / "Generated" / "Output" / args.config / "CG2_0_1.exe"
    if not exe.exists():
        sys.exit(f"exe が見つからない: {exe}（{args.config} でビルドしてから実行する）")

    before = len(read_rows())
    total = args.runs * len(MODES)
    done = 0
    for r in range(args.runs):
        # ラウンドごとに開始方式をずらす（FS→CPU→DS / CPU→DS→FS / DS→FS→CPU …）
        order = MODES[r % len(MODES):] + MODES[:r % len(MODES)]
        for name, mode_args in order:
            done += 1
            code, sec = run_once(exe, mode_args, args.timeout)
            print(f"[{done:2d}/{total}] {name:14s} exit={code} ({sec:.1f}s)")
            if code != 0:
                print("  -> 異常終了。この回の結果は CSV に無い可能性がある")

    rows = read_rows()[before:]
    rows = [row for row in rows if row.get("config") == args.config]
    if not rows:
        sys.exit("今回の計測行が CSV に無い（--bench-textures が効いているか、Logs/load_bench.csv を確認）")

    by_mode = {}
    for row in rows:
        by_mode.setdefault(row["mode"], []).append(row)

    def med(mode, key):
        vals = [float(r[key]) for r in by_mode.get(mode, []) if r.get(key)]
        return statistics.median(vals) if vals else None

    fs_wall = med("FS", "wall_ms")
    fs_cyc = med("FS", "cpu_mcycles")

    lines = []
    sample = rows[0]
    lines.append(f"### テクスチャロード計測（{args.config} / 各 {args.runs} 回の中央値）")
    lines.append("")
    lines.append(f"対象: 全 .dds {sample['textures']} 枚（解凍後 {float(sample['raw_mb']):.1f} MB）。"
                 "計測区間は「読み込み開始 → GPU で使える状態になるまで」。")
    lines.append("")
    lines.append("| 方式 | 読込量 (MB) | 実時間 (ms) | FS 比 | CPU サイクル (M) | CPU 負荷 FS 比 | スループット (MB/s) |")
    lines.append("|---|---:|---:|---:|---:|---:|---:|")
    for name, _ in MODES:
        if name not in by_mode:
            continue
        wall = med(name, "wall_ms")
        cyc = med(name, "cpu_mcycles")
        disk = med(name, "disk_mb")
        thr = med(name, "throughput_mbps")
        speed = f"{fs_wall / wall:.2f}x" if fs_wall and wall else "-"
        cpu_ratio = f"{cyc / fs_cyc * 100:.0f}%" if fs_cyc and cyc is not None else "-"
        lines.append(f"| {name} | {disk:.1f} | {wall:.1f} | {speed} | {cyc:.1f} | {cpu_ratio} | {thr:.0f} |")
    lines.append("")
    lines.append("- 実時間の FS 比は大きいほど速い。CPU 負荷は FS を 100% としたサイクル数の比（小さいほど CPU が空く）。")
    lines.append("- OS のファイルキャッシュが効いた状態（ウォーム）の値。再起動直後の初回（コールド）は別途。")
    md = "\n".join(lines)

    print()
    print(md)
    if args.out:
        out = (PROJECT_DIR / args.out) if not Path(args.out).is_absolute() else Path(args.out)
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_text(md + "\n", encoding="utf-8")
        print(f"\n-> {out}")


if __name__ == "__main__":
    main()
