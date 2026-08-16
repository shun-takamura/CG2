"""キーフレーム削減（誤差許容値ベース）。

「そのキーを削っても、前後のキーからの補間で許容誤差内に再現できるか」を
判定して不要なキーを落とす（Ramer-Douglas-Peucker 法）。
動きの激しい区間はキーが残り、静止・等速の区間は大きく減る。

回転は**クォータニオンとして slerp 補間の角度誤差**で評価する。
（Blender 標準のデシメートは F-Curve のチャンネルを独立に間引くため、
 w/x/y/z がバラバラに削られて微妙な揺れが出ることがある。ここでは
 4成分をひとつの回転として扱うのでその問題が起きない。）

外部依存なしの純ロジック。スタンドアロンアプリ・Blenderアドオン双方から使う。
"""

from __future__ import annotations

import math

_EPS = 1e-8


# ============================================================
# クォータニオン (x, y, z, w) ヘルパー
# ============================================================
def _q_normalize(q):
    n = math.sqrt(sum(c * c for c in q))
    if n < _EPS:
        return (0.0, 0.0, 0.0, 1.0)
    return tuple(c / n for c in q)


def _slerp(a, b, t):
    """球面線形補間。a, b は (x, y, z, w)。"""
    d = sum(x * y for x, y in zip(a, b))
    if d < 0.0:  # 最短経路を通る
        b = tuple(-x for x in b)
        d = -d
    if d > 0.9995:  # ほぼ同じ向きなら線形補間で十分（数値的にも安全）
        return _q_normalize(tuple(a[i] + (b[i] - a[i]) * t for i in range(4)))
    theta0 = math.acos(max(-1.0, min(1.0, d)))
    sin0 = math.sin(theta0)
    if abs(sin0) < _EPS:
        return a
    theta = theta0 * t
    s0 = math.sin(theta0 - theta) / sin0
    s1 = math.sin(theta) / sin0
    return tuple(a[i] * s0 + b[i] * s1 for i in range(4))


def _angle_deg(a, b):
    """2つの回転の角度差（度）。q と -q は同じ回転として扱う。"""
    d = abs(sum(x * y for x, y in zip(a, b)))
    return math.degrees(2.0 * math.acos(max(-1.0, min(1.0, d))))


# ============================================================
# RDP 本体
# ============================================================
def _rdp_keep_indices(count, error_fn, tol):
    """0..count-1 のうち残すインデックス集合を返す。

    error_fn(i0, i1, i): キー i0 と i1 の補間が、実際のキー i をどれだけ
    外しているかの誤差。tol を超える箇所は分割して残す。
    """
    if count <= 2:
        return set(range(count))
    keep = {0, count - 1}
    stack = [(0, count - 1)]
    while stack:
        i0, i1 = stack.pop()
        if i1 - i0 < 2:
            continue
        worst_i, worst_e = -1, -1.0
        for i in range(i0 + 1, i1):
            e = error_fn(i0, i1, i)
            if e > worst_e:
                worst_e, worst_i = e, i
        if worst_e > tol and worst_i > 0:
            keep.add(worst_i)
            stack.append((i0, worst_i))
            stack.append((worst_i, i1))
    return keep


def reduce_rotation_keys(keys, tol_deg):
    """[(time, (x,y,z,w))] を角度誤差 tol_deg 以内で削減する。"""
    if tol_deg <= 0.0 or len(keys) <= 2:
        return list(keys)
    times = [k[0] for k in keys]
    quats = [tuple(k[1]) for k in keys]

    def err(i0, i1, i):
        span = times[i1] - times[i0]
        u = 0.0 if span <= _EPS else (times[i] - times[i0]) / span
        return _angle_deg(_slerp(quats[i0], quats[i1], u), quats[i])

    keep = _rdp_keep_indices(len(keys), err, tol_deg)
    return [keys[i] for i in sorted(keep)]


def reduce_vector_keys(keys, tol):
    """[(time, (x,y,z))] を距離誤差 tol 以内で削減する。"""
    if tol <= 0.0 or len(keys) <= 2:
        return list(keys)
    times = [k[0] for k in keys]
    vecs = [tuple(k[1]) for k in keys]

    def err(i0, i1, i):
        span = times[i1] - times[i0]
        u = 0.0 if span <= _EPS else (times[i] - times[i0]) / span
        approx = tuple(vecs[i0][c] + (vecs[i1][c] - vecs[i0][c]) * u
                       for c in range(3))
        return math.sqrt(sum((approx[c] - vecs[i][c]) ** 2 for c in range(3)))

    keep = _rdp_keep_indices(len(keys), err, tol)
    return [keys[i] for i in sorted(keep)]


def auto_translation_tolerance(trans_keys, ratio=0.002):
    """並進の誤差許容値をデータのスケールから自動決定する。

    モデルの単位系（m / cm など）に依存しないよう、移動範囲の
    対角長の一定割合（既定 0.2%）を許容値とする。
    """
    if not trans_keys or len(trans_keys) <= 2:
        return 0.0
    vals = [k[1] for k in trans_keys]
    extent = 0.0
    for c in range(3):
        col = [v[c] for v in vals]
        extent += (max(col) - min(col)) ** 2
    return math.sqrt(extent) * ratio


# ============================================================
# 結果 dict 単位の削減
# ============================================================
def _stats(before, after):
    return {"before": before, "after": after,
            "ratio": (after / before) if before else 1.0}


def reduce_solved(result, tol_deg, trans_tol=None):
    """rig.solve() の結果を削減した新しい dict を返す。

    q_global_keys（リターゲット用のグローバル回転）は削減しない。
    リターゲットは元の精度で行いたいので、削減は出力時のみに効かせる。
    """
    out = dict(result)
    before = sum(len(v) for v in result["rot_keys"].values())
    out["rot_keys"] = {name: reduce_rotation_keys(keys, tol_deg)
                       for name, keys in result["rot_keys"].items()}
    after = sum(len(v) for v in out["rot_keys"].values())

    if result.get("root_trans_keys"):
        tt = (auto_translation_tolerance(result["root_trans_keys"])
              if trans_tol is None else trans_tol)
        out["root_trans_keys"] = reduce_vector_keys(result["root_trans_keys"], tt)
    out["reduction_stats"] = _stats(before, after)
    return out


def reduce_retargeted(result, tol_deg, trans_tol=None):
    """retarget.retarget() の結果を削減した新しい dict を返す。"""
    out = dict(result)
    before = sum(len(v) for v in result["rot_local_keys"].values())
    out["rot_local_keys"] = {name: reduce_rotation_keys(keys, tol_deg)
                             for name, keys in result["rot_local_keys"].items()}
    after = sum(len(v) for v in out["rot_local_keys"].values())

    if result.get("root_trans_keys"):
        tt = (auto_translation_tolerance(result["root_trans_keys"])
              if trans_tol is None else trans_tol)
        out["root_trans_keys"] = reduce_vector_keys(result["root_trans_keys"], tt)
    out["reduction_stats"] = _stats(before, after)
    return out
