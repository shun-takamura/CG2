"""タイトル画面の遠景の雲のコストを測る（雲あり / 雲なしを同じ秒数ずつ起動して GPU 時間を比べる）。

exe を `--exit-after <秒>` 付きで起動すると、最初のフレームから指定秒で終了する（Framework）。
`--no-clouds` を付けると TitleScene が雲を無効にする（CloudLayer の enabled=0。シェーダは先頭で抜ける）。
このスクリプトはそれを交互に N 回ずつ実行し、各セッションの profile.log から次を集計する。

  - Skybox::Draw の GPU 平均   … 空の描画（雲の計算はここに入る）
  - WaterSurface::Draw の GPU 平均 … 水面（映り込む雲はここに入る）
  - フレーム時間（参考。VSync で頭打ちになるので差は出にくい）

起動直後（--warmup 秒）は読み込みの影響が乗るので捨てる。
実行順は毎ラウンド入れ替える（GPU の温まり具合や実行順による偏りを減らす）。

    py tools\\Python\\run_cloud_bench.py                         # Development / 40 秒 / 2 回ずつ
    py tools\\Python\\run_cloud_bench.py --seconds 60 -n 3
    py tools\\Python\\run_cloud_bench.py --out Documents\\cloud_bench_result.md

作業ディレクトリは Project/（Resources・Logs の相対パスがここ基準）。計測中はマウス・キーに触らないこと。
"""
import argparse
import statistics
import subprocess
import sys
from pathlib import Path

PROJECT_DIR = Path(__file__).resolve().parents[2]
LOGS_DIR = PROJECT_DIR / "Logs"

SECTIONS = ["Skybox::Draw", "WaterSurface::Draw"]
MODES = [
    ("雲あり", []),
    ("雲なし", ["--no-clouds"]),
]


def parse_kv(line):
    kv = {}
    for token in line.split():
        if "=" in token:
            k, v = token.split("=", 1)
            kv[k] = v
    return kv


def session_dirs():
    if not LOGS_DIR.exists():
        return set()
    return {p for p in LOGS_DIR.iterdir() if p.is_dir()}


def summarize(profile_log, warmup):
    """warmup 秒より後の1秒ウィンドウだけで、区間ごとの GPU 平均 [ms] とフレーム時間 [ms] を出す"""
    windows = {}  # frame_id -> (frames, window_s, {section: (gpu_sum_ms, present)})
    with profile_log.open(encoding="utf-8", errors="replace") as f:
        for line in f:
            if " PROFILE " not in line or "section=" not in line:
                continue
            kv = parse_kv(line)
            try:
                frame_id = int(kv["frame"])
                frames = int(kv["frames"])
                window_s = float(kv["window_s"])
            except (KeyError, ValueError):
                continue
            entry = windows.setdefault(frame_id, (frames, window_s, {}))
            entry[2][kv["section"]] = (float(kv.get("gpu_sum_ms", 0.0)), int(kv.get("present", 0)))

    elapsed = 0.0
    total_frames = 0
    total_seconds = 0.0
    gpu = {s: [0.0, 0] for s in SECTIONS}
    for frame_id in sorted(windows):
        frames, window_s, sections = windows[frame_id]
        elapsed += window_s
        if elapsed <= warmup:
            continue
        total_frames += frames
        total_seconds += window_s
        for s in SECTIONS:
            if s in sections:
                gpu[s][0] += sections[s][0]
                gpu[s][1] += sections[s][1]

    result = {s: (gpu[s][0] / gpu[s][1] if gpu[s][1] else float("nan")) for s in SECTIONS}
    result["frame_ms"] = total_seconds * 1000.0 / total_frames if total_frames else float("nan")
    result["measured_s"] = total_seconds
    return result


def run_once(exe, mode_args, seconds, timeout):
    before = session_dirs()
    cmd = [str(exe), "--exit-after", str(seconds)] + mode_args
    proc = subprocess.run(cmd, cwd=PROJECT_DIR, timeout=timeout)
    created = sorted(session_dirs() - before, key=lambda p: p.stat().st_mtime)
    if proc.returncode != 0:
        print(f"  [WARN] exit code {proc.returncode}")
    if not created:
        raise RuntimeError("新しいセッションフォルダ（Logs/）が見つからない")
    log = created[-1] / "profile.log"
    if not log.exists():
        raise RuntimeError(f"profile.log が無い: {log}（USE_PEPPER が有効な Debug / Development で実行する）")
    return created[-1], log


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--config", default="Development", help="Generated/Output/<config>/CG2_0_1.exe を使う")
    ap.add_argument("--seconds", type=float, default=40.0, help="1回あたりの起動時間 [s]")
    ap.add_argument("--warmup", type=float, default=5.0, help="先頭から捨てる秒数")
    ap.add_argument("-n", type=int, default=2, help="各モードの回数")
    ap.add_argument("--out", type=Path, help="結果の Markdown 表を書き出す")
    args = ap.parse_args(argv)

    exe = PROJECT_DIR.parent / "Generated" / "Output" / args.config / "CG2_0_1.exe"
    if not exe.exists():
        sys.exit(f"exe が見つからない: {exe}（{args.config} でビルドしてから実行する）")

    results = {name: [] for name, _ in MODES}
    timeout = args.seconds + 120.0  # 起動・読み込み・終了処理のぶんの余裕
    for round_index in range(args.n):
        order = MODES if round_index % 2 == 0 else list(reversed(MODES))
        for name, mode_args in order:
            print(f"[{round_index + 1}/{args.n}] {name} を {args.seconds:.0f} 秒実行中...", flush=True)
            session, log = run_once(exe, mode_args, args.seconds, timeout)
            r = summarize(log, args.warmup)
            results[name].append(r)
            print(f"  {session.name}: " + " / ".join(f"{s} {r[s]:.3f}ms" for s in SECTIONS)
                  + f" / frame {r['frame_ms']:.2f}ms（{r['measured_s']:.0f}s 分）")

    def median(name, key):
        return statistics.median(r[key] for r in results[name])

    with_name, without_name = MODES[0][0], MODES[1][0]
    lines = [
        f"| 区間（GPU 平均, {args.config}, {args.seconds:.0f}s×{args.n}回, 先頭{args.warmup:.0f}s除外, 中央値） | 雲あり [ms] | 雲なし [ms] | 差 [ms] |",
        "|---|---:|---:|---:|",
    ]
    total_diff = 0.0
    for key in SECTIONS + ["frame_ms"]:
        a = median(with_name, key)
        b = median(without_name, key)
        label = "フレーム時間（参考・VSync）" if key == "frame_ms" else f"`{key}`"
        lines.append(f"| {label} | {a:.3f} | {b:.3f} | {a - b:+.3f} |")
        if key in SECTIONS:
            total_diff += a - b
    lines.append(f"| **雲のコスト合計（空＋水面）** | | | **{total_diff:+.3f}** |")

    table = "\n".join(lines)
    print()
    print(table)
    if args.out:
        args.out.write_text(table + "\n", encoding="utf-8")
        print(f"\nwrote {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
