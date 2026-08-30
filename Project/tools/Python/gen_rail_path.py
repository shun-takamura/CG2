#!/usr/bin/env python3
"""レールカメラの走行スプライン（CameraPathSpline）を生成してシーンJSONへ書き込む。

背景と制約（変更する前に必ず読むこと）:

* エンジンの `SplineCurveActor::Sample` は **弧長ではなく制御点インデックスで等分** する。
  よって

      - 制御点の「個数」   = 時間配分   （1セクション1分なら物理長に関係なく等しい点数）
      - 制御点の「間隔」   = 速度       （間隔が広い区間ほど速く通過する）

  という対応になる。3秒/点で 60 点 = 180 秒。

* **制御点を後から挿入・削除すると、それ以降の進行度 t が全部ずれる**
  （`camera.rotKeys[].t` と Wave の `trigger_sec` が破綻する）。
  A-2 で 60 点に凍結し、以後は「点を動かすだけ」にすること。

* 座標系は +Z が前方、+Y が上。地面 Y=0、谷底 Y=-150 を前提にしている。

使い方:
    python tools/Python/gen_rail_path.py            # 書き込み（.bak を残す）
    python tools/Python/gen_rail_path.py --dry-run  # 数値だけ確認
"""

import argparse
import json
import math
import os
import shutil

SCENE_PATH = "Resources/Json/Scenes/StagePlay.json"

# ---------------------------------------------------------------- 形状パラメータ
TOTAL_POINTS = 60          # 凍結。変えると t の対応が全部ずれる
SEC_PER_POINT = 3.0        # 60 点 × 3 秒 = 180 秒

Y_SKY = 900.0              # 高高度の巡航高度
Y_LOW = 40.0               # 地上低空飛行の高度（地面 Y=0）
Y_CANYON = -60.0           # 谷の中（谷底 Y=-150 想定）

# 降下の区間（インデックス）。セクション境界の blendSec 窓と秒を揃えてある。
DIVE_CLOUD = (17, 23)      # 雲抜け急降下   idx17(51s) → idx23(69s)。窓は 53-67s
DIVE_CANYON = (39, 41)     # 谷への落下     idx39(117s) → idx41(123s)。窓は 117-123s

SPACING_SKY = 70.0         # 水平間隔[m]/点
SPACING_DIVE = 40.0        # 降下中は水平を詰める（3D距離が伸びすぎないように）
SPACING_LOW = 65.0
SPACING_CANYON = 50.0

AMP_SKY = 80.0             # S字の振れ幅[m]
AMP_LOW = 40.0
AMP_CANYON = 35.0

WAVELEN_SKY = 900.0        # S字の波長[m]
WAVELEN_LOW = 700.0
WAVELEN_CANYON = 400.0


def smoothstep(a, b, x):
    """x を [a,b] で 0→1 に滑らかに写す。"""
    if b <= a:
        return 0.0 if x < a else 1.0
    t = min(max((x - a) / (b - a), 0.0), 1.0)
    return t * t * (3.0 - 2.0 * t)


def altitude(idx):
    """インデックス→高度。2 回の降下を smoothstep で繋ぐ。"""
    y = Y_SKY
    y += (Y_LOW - Y_SKY) * smoothstep(DIVE_CLOUD[0], DIVE_CLOUD[1], idx)
    y += (Y_CANYON - Y_LOW) * smoothstep(DIVE_CANYON[0], DIVE_CANYON[1], idx)
    return y


def _blend3(idx, sky, low, canyon):
    """sky→low→canyon を降下区間で滑らかに切り替える共通ヘルパ。"""
    v = sky
    v += (low - sky) * smoothstep(DIVE_CLOUD[0], DIVE_CLOUD[1], idx)
    v += (canyon - low) * smoothstep(DIVE_CANYON[0], DIVE_CANYON[1], idx)
    return v


def spacing(idx):
    """セグメント idx→idx+1 の水平間隔。降下中だけ詰める。"""
    base = _blend3(idx, SPACING_SKY, SPACING_LOW, SPACING_CANYON)
    # 降下の最中は水平を SPACING_DIVE 側へ寄せる（降下量で 3D 長が伸びるため）
    in_dive = max(
        smoothstep(DIVE_CLOUD[0], DIVE_CLOUD[0] + 1, idx) * (1.0 - smoothstep(DIVE_CLOUD[1] - 1, DIVE_CLOUD[1], idx)),
        smoothstep(DIVE_CANYON[0], DIVE_CANYON[0] + 0.5, idx) * (1.0 - smoothstep(DIVE_CANYON[1] - 0.5, DIVE_CANYON[1], idx)),
    )
    return base + (SPACING_DIVE - base) * in_dive


def build_points():
    pts = []
    z = 0.0
    phase = 0.0
    for i in range(TOTAL_POINTS):
        amp = _blend3(i, AMP_SKY, AMP_LOW, AMP_CANYON)
        x = amp * math.sin(phase)
        pts.append([round(x, 3), round(altitude(i), 3), round(z, 3)])

        seg = spacing(i)
        wav = _blend3(i, WAVELEN_SKY, WAVELEN_LOW, WAVELEN_CANYON)
        # 位相は距離で進める＝区間をまたいでも折れ目が出ない
        phase += 2.0 * math.pi * seg / wav
        z += seg
    return pts


def report(pts):
    def d(a, b):
        return math.dist(a, b)

    segs = [d(pts[i], pts[i + 1]) for i in range(len(pts) - 1)]
    total = sum(segs)
    print(f"points   : {len(pts)}  ({SEC_PER_POINT}s/点 → {len(pts) * SEC_PER_POINT:.0f}s)")
    print(f"total    : {total:.0f} m   平均 {total / (len(pts) - 1) / SEC_PER_POINT:.1f} m/s")
    print(f"Y range  : {min(p[1] for p in pts):.0f} .. {max(p[1] for p in pts):.0f}")
    bounds = [("SkyAboveClouds", 0, 20), ("LowAltitude", 20, 40), ("Canyon", 40, 60)]
    for name, a, b in bounds:
        s = sum(segs[i] for i in range(a, min(b, len(segs))))
        n = min(b, len(segs)) - a
        print(f"  {name:<15} {s:7.0f} m   {s / (n * SEC_PER_POINT):5.1f} m/s")
    fastest = max(range(len(segs)), key=lambda i: segs[i])
    print(f"最速セグメント: idx{fastest} ({fastest * SEC_PER_POINT:.0f}s) "
          f"{segs[fastest]:.0f}m → {segs[fastest] / SEC_PER_POINT:.1f} m/s")


def find_spline(doc, name):
    for obj in doc.get("objects", []):
        if obj.get("type") == "Spline" and obj.get("name") == name:
            return obj
    return None


def relocate_enemy_path(doc, cam_pts):
    """EnemyPath_01 を新レール脇へ平行移動する（Phase E までの暫定措置）。

    形は変えず、重心を「ステージ 39 秒地点のカメラ位置 + 右へ 30m」に合わせるだけ。
    """
    sp = find_spline(doc, "EnemyPath_01")
    if not sp or not sp.get("points"):
        print("EnemyPath_01 が見つからないのでスキップ")
        return
    pts = sp["points"]
    n = len(pts)
    cx = sum(p[0] for p in pts) / n
    cy = sum(p[1] for p in pts) / n
    cz = sum(p[2] for p in pts) / n

    # 序盤のドローン3体（trigger 6.25/12.5/18.75s）が見える位置に合わせる。
    # 1本のスプラインで全エントリの時刻には合わせられないので、あくまで暫定。
    anchor = cam_pts[5]  # idx5 = 15 秒地点
    tx, ty, tz = anchor[0] + 30.0, anchor[1], anchor[2]
    dx, dy, dz = tx - cx, ty - cy, tz - cz
    sp["points"] = [[round(p[0] + dx, 3), round(p[1] + dy, 3), round(p[2] + dz, 3)] for p in pts]
    print(f"EnemyPath_01 を移設: delta=({dx:.0f}, {dy:.0f}, {dz:.0f})")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dry-run", action="store_true", help="書き込まず数値だけ表示")
    ap.add_argument("--scene", default=SCENE_PATH)
    args = ap.parse_args()

    pts = build_points()
    report(pts)
    if args.dry_run:
        return

    if not os.path.exists(args.scene):
        raise SystemExit(f"シーンJSONが見つからない: {args.scene}")

    with open(args.scene, encoding="utf-8") as f:
        doc = json.load(f)

    cam = find_spline(doc, "CameraPath")
    if cam is None:
        raise SystemExit("CameraPath（CameraPathSpline）がシーンJSONに無い")
    old = len(cam.get("points", []))
    cam["points"] = pts

    relocate_enemy_path(doc, pts)

    # 既存の .bak は上書きしない（2回目の実行で「加工済み」がバックアップに化けるのを防ぐ）
    bak = args.scene + ".bak"
    if not os.path.exists(bak):
        shutil.copyfile(args.scene, bak)
    else:
        print(f"（{bak} は既存のため温存）")
    with open(args.scene, "w", encoding="utf-8") as f:
        json.dump(doc, f, indent=2, ensure_ascii=False)
    print(f"\n書き込み完了: {args.scene}  (CameraPath {old} → {len(pts)} 点)")
    print(f"バックアップ : {args.scene}.bak")


if __name__ == "__main__":
    main()
