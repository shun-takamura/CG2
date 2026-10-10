"""雲のコストを測る（モードごとに同じ秒数ずつ起動して、GPU 時間と PS の実行回数を比べる）。

exe を `--exit-after <秒>` 付きで起動すると、最初のフレームから指定秒で終了する（Framework）。
モード＝起動引数の組。各モードを交互に N 回ずつ実行し、各セッションの profile.log から次を集計する。

  - 区間ごとの GPU 平均 [ms]（--sections）
  - パイプライン統計のカウンタ（PEPPER_GPU_STATS_SCOPE の「区間名.PSInv」）の 1 フレーム平均
    → 画面の画素数で割ると「平均の重なり枚数（オーバードロー）」
  - フレーム時間（参考。VSync で頭打ちになるので差は出にくい）

起動直後（--warmup 秒）は読み込みの影響が乗るので捨てる。
実行順は毎ラウンド入れ替える（GPU の温まり具合や実行順による偏りを減らす）。

プリセット:
  title         タイトルの遠景の雲（雲あり / --no-clouds）。フェーズ1 §4.2 の計測
  stage-planes  ステージの板の雲。--start-scene STAGEPLAY --bench-hold <秒> で同じ画面に止め、
                板なし / 今の板（StagePlay.json）/ gen_cloud_plane_bench.py の N 枚 を比べる
  stage-raymarch 雲海のレイマーチ。--holds の各秒で 雲海あり / --no-raymarch-clouds を比べる
                （既定 0 = 雲海の上、53 = 入口の B'、60 = 出口の B'）

    py tools\\Python\\run_cloud_bench.py                                   # title（従来どおり）
    py tools\\Python\\gen_cloud_plane_bench.py -n 0 100 200 400 800
    py tools\\Python\\run_cloud_bench.py --preset stage-planes --planes 100 200 400 800 --seconds 20
    py tools\\Python\\run_cloud_bench.py --preset stage-raymarch --seconds 20
    py tools\\Python\\run_cloud_bench.py --mode "A=--foo" --mode "B=--bar" --sections Skybox::Draw

作業ディレクトリは Project/（Resources・Logs の相対パスがここ基準）。計測中はマウス・キーに触らないこと。
GPU は軽い負荷だとクロックを下げるので、軽いモードほど遅めに出る（9_CloudRendering §4.2）。
"""
import argparse
import shlex
import statistics
import subprocess
import sys
from pathlib import Path

PROJECT_DIR = Path(__file__).resolve().parents[2]
LOGS_DIR = PROJECT_DIR / "Logs"
BENCH_DIR_FROM_PROJECT = "../Generated/Bench"  # gen_cloud_plane_bench.py の出力（Project/ 基準）
SCREEN_PIXELS = 1600 * 900                     # WindowsApplication::kClientWidth × kClientHeight

PRESETS = {
    "title": {
        "sections": ["Skybox::Draw", "WaterSurface::Draw"],
        "base_args": [],
    },
    "stage-planes": {
        "sections": ["Scene::DrawDynamicPrimitives", "Skybox::Draw", "Scene::DrawDynamicObjects"],
        "base_args": ["--start-scene", "STAGEPLAY"],
    },
    "stage-raymarch": {
        "sections": ["CloudRaymarcher::March", "CloudRaymarcher::Composite", "Skybox::Draw", "Scene::DrawDynamicObjects"],
        "base_args": ["--start-scene", "STAGEPLAY"],
    },
}


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


def summarize(profile_log, warmup, sections):
    """warmup 秒より後の1秒ウィンドウだけで、区間の GPU 平均・カウンタの1フレーム平均・フレーム時間を出す"""
    windows = {}  # frame_id -> {"frames", "window_s", "sec": {name: (gpu_sum, present)}, "cnt": {name: total}}
    with profile_log.open(encoding="utf-8", errors="replace") as f:
        for line in f:
            if " PROFILE " not in line:
                continue
            kv = parse_kv(line)
            try:
                frame_id = int(kv["frame"])
                frames = int(kv["frames"])
                window_s = float(kv["window_s"])
            except (KeyError, ValueError):
                continue
            w = windows.setdefault(frame_id, {"frames": frames, "window_s": window_s, "sec": {}, "cnt": {}})
            if "section" in kv:
                w["sec"][kv["section"]] = (float(kv.get("gpu_sum_ms", 0.0)), int(kv.get("present", 0)))
            elif "counter" in kv:
                w["cnt"][kv["counter"]] = int(kv.get("total", 0))

    elapsed = 0.0
    total_frames = 0
    total_seconds = 0.0
    gpu = {s: [0.0, 0] for s in sections}
    counters = {}
    for frame_id in sorted(windows):
        w = windows[frame_id]
        elapsed += w["window_s"]
        if elapsed <= warmup:
            continue
        total_frames += w["frames"]
        total_seconds += w["window_s"]
        for s in sections:
            if s in w["sec"]:
                gpu[s][0] += w["sec"][s][0]
                gpu[s][1] += w["sec"][s][1]
        for name, total in w["cnt"].items():
            if name.endswith(".PSInv") or name.endswith(".Prims"):
                counters[name] = counters.get(name, 0) + total

    result = {s: (gpu[s][0] / gpu[s][1] if gpu[s][1] else float("nan")) for s in sections}
    for name, total in counters.items():
        result[name] = total / total_frames if total_frames else float("nan")
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


def build_modes(args, preset):
    """(名前, 起動引数) の並び。--mode があればそれだけを使う"""
    base = list(preset["base_args"])
    if args.start_scene:
        base = [a for a in base if a not in ("--start-scene", "STAGEPLAY")] + ["--start-scene", args.start_scene]
    if args.preset == "stage-raymarch":
        modes = []
        for sec in args.holds:
            hold = base + ["--bench-hold", str(sec)]
            modes.append((f"{sec:g}s 雲海あり", hold))
            modes.append((f"{sec:g}s 雲海なし", hold + ["--no-raymarch-clouds"]))
        return modes
    if args.preset == "stage-planes" or args.bench_hold is not None:
        base += ["--bench-hold", str(args.bench_hold if args.bench_hold is not None else 0.0)]

    if args.mode:
        modes = []
        for m in args.mode:
            name, _, rest = m.partition("=")
            modes.append((name, base + shlex.split(rest)))
        return modes
    if args.preset == "title":
        return [("雲あり", base), ("雲なし", base + ["--no-clouds"])]
    # stage-planes：板なし（基準）/ 今の板 / N 枚
    modes = [("板なし", base + ["--scene-json", f"{BENCH_DIR_FROM_PROJECT}/CloudPlanes_0.json"]),
             ("今の板", base)]
    for n in args.planes:
        modes.append((f"{n}枚", base + ["--scene-json", f"{BENCH_DIR_FROM_PROJECT}/CloudPlanes_{n}.json"]))
    return modes


def check_scene_files(modes):
    for name, margs in modes:
        if "--scene-json" in margs:
            p = PROJECT_DIR / margs[margs.index("--scene-json") + 1]
            if not p.exists():
                sys.exit(f"{name}: {p} が無い（gen_cloud_plane_bench.py -n ... で作る）")


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--preset", choices=sorted(PRESETS), default="title")
    ap.add_argument("--config", default="Development", help="Generated/Output/<config>/CG2_0_1.exe を使う")
    ap.add_argument("--seconds", type=float, default=40.0, help="1回あたりの起動時間 [s]")
    ap.add_argument("--warmup", type=float, default=5.0, help="先頭から捨てる秒数")
    ap.add_argument("-n", type=int, default=2, help="各モードの回数")
    ap.add_argument("--sections", help="集計する GPU 区間（カンマ区切り。省略時はプリセットの既定）")
    ap.add_argument("--start-scene", help="最初のシーン（例: STAGEPLAY）")
    ap.add_argument("--bench-hold", type=float, help="StagePlay でこの秒へシークしてレールを止める（stage-planes の既定 0）")
    ap.add_argument("--planes", type=int, nargs="*", default=[], help="stage-planes で比べる板の枚数（CloudPlanes_<N>.json）")
    ap.add_argument("--holds", type=float, nargs="*", default=[0.0, 53.0, 60.0], help="stage-raymarch で止める秒")
    ap.add_argument("--mode", action="append", help='"名前=起動引数" を並べて任意のモードを比べる（複数指定）')
    ap.add_argument("--out", type=Path, help="結果の Markdown 表を書き出す")
    args = ap.parse_args(argv)

    preset = PRESETS[args.preset]
    sections = args.sections.split(",") if args.sections else preset["sections"]
    modes = build_modes(args, preset)
    check_scene_files(modes)

    exe = PROJECT_DIR.parent / "Generated" / "Output" / args.config / "CG2_0_1.exe"
    if not exe.exists():
        sys.exit(f"exe が見つからない: {exe}（{args.config} でビルドしてから実行する）")

    results = {name: [] for name, _ in modes}
    timeout = args.seconds + 180.0  # 起動・読み込み・終了処理のぶんの余裕
    for round_index in range(args.n):
        order = modes if round_index % 2 == 0 else list(reversed(modes))
        for name, mode_args in order:
            print(f"[{round_index + 1}/{args.n}] {name} を {args.seconds:.0f} 秒実行中... {' '.join(mode_args)}", flush=True)
            session, log = run_once(exe, mode_args, args.seconds, timeout)
            r = summarize(log, args.warmup, sections)
            results[name].append(r)
            print(f"  {session.name}: " + " / ".join(f"{s} {r[s]:.3f}ms" for s in sections)
                  + f" / frame {r['frame_ms']:.2f}ms（{r['measured_s']:.0f}s 分）")

    def median(name, key):
        vals = [r[key] for r in results[name] if key in r]
        return statistics.median(vals) if vals else float("nan")

    counter_keys = sorted({k for rs in results.values() for r in rs for k in r if k.endswith(".PSInv")})
    names = [name for name, _ in modes]
    ref = names[-1] if args.preset == "title" else names[0]  # title は「雲なし」、他は先頭を基準に差を取る

    head = f"| モード（{args.config}, {args.seconds:.0f}s×{args.n}回, 先頭{args.warmup:.0f}s除外, 中央値） | "
    cols = [f"`{s}` [ms]" for s in sections]
    for c in counter_keys:
        cols += [f"`{c}` [/frame]", "重なり [画面枚]"]
    cols += ["フレーム [ms]", f"差（{ref} 比, 先頭区間）[ms]"]
    lines = [head + " | ".join(cols) + " |", "|---|" + "---:|" * len(cols)]
    for name in names:
        row = [f"{median(name, s):.3f}" for s in sections]
        for c in counter_keys:
            ps = median(name, c)
            row += [f"{ps:,.0f}", f"{ps / SCREEN_PIXELS:.2f}"]
        row += [f"{median(name, 'frame_ms'):.2f}", f"{median(name, sections[0]) - median(ref, sections[0]):+.3f}"]
        lines.append(f"| {name} | " + " | ".join(row) + " |")

    # 板の雲：重なり 1 画面枚あたりの GPU 時間（基準との差 ÷ 重なりの差）
    if counter_keys and args.preset == "stage-planes":
        c = counter_keys[0]
        lines.append("")
        lines.append(f"| モード | 増えた重なり [画面枚] | 増えた GPU [ms] | 1 画面枚あたり [ms] |")
        lines.append("|---|---:|---:|---:|")
        for name in names[1:]:
            d_layers = (median(name, c) - median(ref, c)) / SCREEN_PIXELS
            d_ms = median(name, sections[0]) - median(ref, sections[0])
            per = d_ms / d_layers if d_layers > 1e-6 else float("nan")
            lines.append(f"| {name} | {d_layers:.2f} | {d_ms:+.3f} | {per:.4f} |")

    table = "\n".join(lines)
    print()
    print(table)
    if args.out:
        args.out.write_text(table + "\n", encoding="utf-8")
        print(f"\nwrote {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
