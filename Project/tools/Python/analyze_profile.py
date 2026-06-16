#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
P.E.P.P.E.R. profile.log 解析ツール

profile.log（1秒ウィンドウ集計）を読み、区間別の負荷を集計して
「最遅区間 TopN」を file:line 付きで提示する。CPU優位／GPU優位を区別し、
フレーム予算（既定 60fps=16.67ms）に対する割合も出す。
最適化提案（Step7）に渡せるよう --json でも出力できる。

使い方:
    python analyze_profile.py                 # 最新セッションを自動検出
    python analyze_profile.py <session_dir>   # セッションフォルダ指定
    python analyze_profile.py <profile.log>   # ファイル直接指定
    python analyze_profile.py --fps 240 --top 15
    python analyze_profile.py --json          # 機械可読（Claude連携用）
"""

import argparse
import json
import sys
from pathlib import Path


def reconfigure_stdout():
    # Windows コンソールでも日本語を出せるように
    try:
        sys.stdout.reconfigure(encoding="utf-8")
    except Exception:
        pass


def find_profile_log(arg_path):
    """引数（無指定/フォルダ/ファイル）から profile.log の実体を解決する。"""
    if arg_path:
        p = Path(arg_path)
        if p.is_file():
            return p
        if p.is_dir():
            cand = p / "profile.log"
            if cand.is_file():
                return cand
            # Logs ルートを渡された場合は最新セッションを探す
            return latest_in_logs_root(p)
        raise FileNotFoundError(f"パスが見つからない: {arg_path}")

    # 無指定: ログ出力先（Project/Logs）を探す。実行場所やスクリプト位置に依存しないよう、
    # カレントと自分の祖先を辿って "Logs" / "Project/Logs" を見つける。
    here = Path(__file__).resolve()
    bases = [Path.cwd(), *Path.cwd().parents, *here.parents]
    for base in bases:
        for root in (base / "Logs", base / "Project" / "Logs"):
            if root.is_dir():
                log = latest_in_logs_root(root)
                if log:
                    return log
    raise FileNotFoundError("profile.log が見つからない。パスを指定してください。")


def latest_in_logs_root(root):
    """Logs ルート配下で profile.log を持つ最新セッションフォルダを返す。"""
    sessions = [d for d in root.iterdir() if d.is_dir() and (d / "profile.log").is_file()]
    if not sessions:
        return None
    latest = max(sessions, key=lambda d: d.stat().st_mtime)
    return latest / "profile.log"


def parse_kv(line):
    """1行から key=value を辞書化する（先頭の時刻/カテゴリ/レベルは '=' が無いので無視される）。"""
    kv = {}
    for tok in line.split():
        if "=" in tok:
            k, _, v = tok.partition("=")
            kv[k] = v
    return kv


def to_float(d, k, default=0.0):
    try:
        return float(d.get(k, default))
    except ValueError:
        return default


def to_int(d, k, default=0):
    try:
        return int(d.get(k, default))
    except ValueError:
        return default


def analyze(log_path, min_frames):
    """profile.log を読み、区間/カウンタ/ウィンドウを集計する。"""
    sections = {}   # name -> 集計
    counters = {}   # name -> 集計
    windows = {}    # frame_id -> (frames, window_s)  ※重複ウィンドウの二重計上を防ぐ

    with open(log_path, "r", encoding="utf-8", errors="replace") as f:
        for line in f:
            if "PROFILE" not in line:
                continue
            kv = parse_kv(line)
            frames = to_int(kv, "frames")
            # コールドスタート（ロード）ウィンドウなどフレーム数が極端に少ない窓は除外
            if frames < min_frames:
                continue

            frame_id = kv.get("frame")
            if frame_id is not None:
                windows[frame_id] = (frames, to_float(kv, "window_s"))

            if "section" in kv:
                name = kv["section"]
                s = sections.setdefault(name, {
                    "file": kv.get("file", ""), "line": to_int(kv, "line"),
                    "depth": to_int(kv, "depth"),
                    "sum_cpu": 0.0, "sum_gpu": 0.0, "frames": 0,
                    "present": 0, "calls": 0, "max_cpu": 0.0, "max_gpu": 0.0,
                })
                s["sum_cpu"] += to_float(kv, "sum_ms")
                s["sum_gpu"] += to_float(kv, "gpu_sum_ms")
                s["frames"] += frames
                s["present"] += to_int(kv, "present")
                s["calls"] += to_int(kv, "calls")
                s["max_cpu"] = max(s["max_cpu"], to_float(kv, "max_ms"))
                s["max_gpu"] = max(s["max_gpu"], to_float(kv, "gpu_max_ms"))

            elif "counter" in kv:
                name = kv["counter"]
                c = counters.setdefault(name, {"total": 0, "frames": 0, "max_per_frame": 0})
                c["total"] += to_int(kv, "total")
                c["frames"] += frames
                c["max_per_frame"] = max(c["max_per_frame"], to_int(kv, "max_per_frame"))

    total_frames = sum(fr for fr, _ in windows.values())
    total_secs = sum(ws for _, ws in windows.values())
    avg_fps = (total_frames / total_secs) if total_secs > 0 else 0.0

    # 区間ごとの1フレームあたり寄与を確定
    rows = []
    for name, s in sections.items():
        fr = s["frames"] if s["frames"] > 0 else 1
        cpu_pf = s["sum_cpu"] / fr
        gpu_pf = s["sum_gpu"] / fr
        calls = s["calls"]
        rows.append({
            "section": name, "file": s["file"], "line": s["line"], "depth": s["depth"],
            "cpu_per_frame": cpu_pf, "gpu_per_frame": gpu_pf,
            "calls_per_frame": calls / fr,
            # 1回あたりCPUコスト（µs）。「1回が重い」か「回数で重い」かを切り分ける
            "cpu_per_call_us": (s["sum_cpu"] / calls * 1000.0) if calls > 0 else 0.0,
            "max_cpu": s["max_cpu"], "max_gpu": s["max_gpu"],
            "dominant": "GPU" if gpu_pf > cpu_pf else "CPU",
        })

    counter_rows = []
    for name, c in counters.items():
        fr = c["frames"] if c["frames"] > 0 else 1
        counter_rows.append({
            "counter": name, "avg_per_frame": c["total"] / fr,
            "max_per_frame": c["max_per_frame"], "total": c["total"],
        })

    return {
        "session": log_path.parent.name,
        "log_path": str(log_path),
        "total_frames": total_frames,
        "windows": len(windows),
        "avg_fps": avg_fps,
        "sections": rows,
        "counters": counter_rows,
    }


def build_hints(result, calls_warn, drawcall_warn):
    """時間ランキングに埋もれがちな「無駄候補」をヒューリスティックであぶり出す（確定でなく候補）。"""
    hints = []

    # DrawCall 過多 → 描画コール削減（バッチ化/インスタンシング）の余地
    for c in result["counters"]:
        if c["counter"] == "DrawCall" and c["avg_per_frame"] >= drawcall_warn:
            hints.append(
                f"DrawCall が平均 {c['avg_per_frame']:.0f}/フレーム（閾値{drawcall_warn}）"
                f" → 描画コール削減（バッチ化/インスタンシング/アトラス化）候補")

    # 呼び出し回数が突出 → まとめ/キャッシュ候補（1回が安くても回数で効く）
    for r in sorted(result["sections"], key=lambda r: r["calls_per_frame"], reverse=True):
        if r["calls_per_frame"] >= calls_warn:
            loc = f"{r['file']}:{r['line']}" if r["file"] else "-"
            hints.append(
                f"{r['section']}: 呼び出し {r['calls_per_frame']:.0f}/フレーム が多い"
                f"（1回 {r['cpu_per_call_us']:.1f}µs）→ まとめ/キャッシュ/呼び出し削減候補 [{loc}]")

    return hints


def print_efficiency(result, top, hints):
    print("== 効率チェック（無駄あぶり出し）==")
    print("  -- 呼び出し回数 TOP（calls/f 順。回数で効く無駄の発見用）--")
    print(f"  {'#':>2}  {'区間':<32}{'呼出/f':>8}  {'µs/回':>8}  {'ms/f':>8}  位置")
    by_calls = sorted(result["sections"], key=lambda r: r["calls_per_frame"], reverse=True)
    for i, r in enumerate(by_calls[:top], 1):
        loc = f"{r['file']}:{r['line']}" if r["file"] else "-"
        print(f"  {i:>2}  {r['section']:<32}{r['calls_per_frame']:>8.1f}  "
              f"{r['cpu_per_call_us']:>8.1f}  {r['cpu_per_frame']:>8.3f}  {loc}")
    print()
    print("  -- 気になる候補 --")
    if hints:
        for h in hints:
            print(f"  ⚠ {h}")
    else:
        print("  （閾値を超える明らかな候補なし。深い所の無駄は計装を細かくするか Step7 で）")
    print()


def print_report(result, fps, top, hints):
    budget = 1000.0 / fps
    print(f"P.E.P.P.E.R. プロファイル解析")
    print(f"  セッション : {result['session']}")
    print(f"  ログ       : {result['log_path']}")
    print(f"  解析フレーム: {result['total_frames']}  ウィンドウ: {result['windows']}  "
          f"平均FPS: {result['avg_fps']:.1f}")
    print(f"  フレーム予算: {budget:.2f} ms ({fps:.0f} FPS)")
    print()

    def table(title, rows, key, ms_key):
        print(f"== {title} (1フレームあたり平均) ==")
        print(f"  {'#':>2}  {'区間':<32}{'D':>2}  {'ms/f':>8}  {'%予算':>6}  "
              f"{'呼出/f':>7}  {'最大ms':>8}  位置")
        for i, r in enumerate(rows[:top], 1):
            ms = r[ms_key]
            pct = ms / budget * 100.0
            loc = f"{r['file']}:{r['line']}" if r["file"] else "-"
            mark = "◀GPU" if (key == "cpu_per_frame" and r["dominant"] == "GPU") else ""
            print(f"  {i:>2}  {r['section']:<32}{r['depth']:>2}  {ms:>8.3f}  "
                  f"{pct:>5.1f}%  {r['calls_per_frame']:>7.1f}  "
                  f"{r['max_cpu'] if ms_key=='cpu_per_frame' else r['max_gpu']:>8.3f}  {loc} {mark}")
        print()

    cpu_sorted = sorted(result["sections"], key=lambda r: r["cpu_per_frame"], reverse=True)
    table("CPU 最遅区間 TOP", cpu_sorted, "cpu_per_frame", "cpu_per_frame")

    gpu_sorted = sorted(result["sections"], key=lambda r: r["gpu_per_frame"], reverse=True)
    gpu_sorted = [r for r in gpu_sorted if r["gpu_per_frame"] > 0.0]
    if gpu_sorted:
        table("GPU 最遅区間 TOP", gpu_sorted, "gpu_per_frame", "gpu_per_frame")

    if result["counters"]:
        print("== カウンタ ==")
        for c in result["counters"]:
            print(f"  {c['counter']}: 平均 {c['avg_per_frame']:.1f} /フレーム  "
                  f"最大 {c['max_per_frame']}  (累計 {c['total']})")
        print()

    print_efficiency(result, top, hints)

    print("== 注記 ==")
    print("  depth=0/1 は子区間を含む合計（親）。最適化は深い（葉）区間の重い所から。")
    print("  ◀GPU 印は CPU 表に出ているが実際は GPU 律速の区間。")
    print("  時間ランキングは『重い処理』、効率チェックは『回数で効く無駄』。両輪で見る。")


def main():
    reconfigure_stdout()
    ap = argparse.ArgumentParser(description="P.E.P.P.E.R. profile.log アナライザ")
    ap.add_argument("path", nargs="?", help="セッションフォルダ / profile.log / Logs ルート（無指定で最新）")
    ap.add_argument("--fps", type=float, default=60.0, help="フレーム予算の基準FPS（既定60）")
    ap.add_argument("--top", type=int, default=10, help="表示する区間数（既定10）")
    ap.add_argument("--min-frames", type=int, default=10,
                    help="このフレーム数未満のウィンドウは無視（コールドスタート除外。既定10）")
    ap.add_argument("--calls-warn", type=float, default=100.0,
                    help="呼び出し回数/フレームがこれ以上の区間を無駄候補として警告（既定100）")
    ap.add_argument("--drawcall-warn", type=float, default=100.0,
                    help="DrawCall/フレームがこれ以上なら描画コール削減候補として警告（既定100）")
    ap.add_argument("--json", action="store_true", help="JSONで出力（Claude連携用）")
    args = ap.parse_args()

    log_path = find_profile_log(args.path)
    result = analyze(log_path, args.min_frames)
    hints = build_hints(result, args.calls_warn, args.drawcall_warn)
    result["hints"] = hints

    if args.json:
        result["fps_budget_ms"] = 1000.0 / args.fps
        print(json.dumps(result, ensure_ascii=False, indent=2))
    else:
        print_report(result, args.fps, args.top, hints)


if __name__ == "__main__":
    main()
