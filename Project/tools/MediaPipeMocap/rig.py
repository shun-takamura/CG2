"""MediaPipe Pose の 33 ワールドランドマークを 21 ジョイントのスケルトンに変換する。

方針:
- バインドポーズ = 最初の有効フレーム。全ジョイントのグローバル回転は単位で、
  ローカル並進 = 親からのグローバル差分ベクトル（→ 逆バインド行列は translate(-p) だけで済む）。
- 各フレームのジョイント回転は「バインド時のボーン方向 → 現フレームのボーン方向」への
  最短回転（swing）で近似する。ひねり（twist/roll）は単眼推定では取れないため再現しない。
- Hips / Chest だけは左右のランドマーク線と上方向から直交基底を組み、ひねり込みの回転を出す
  （胴体の向きはアニメーションの見た目への影響が大きいため）。

座標系: MediaPipe world (x=右, y=下, z=カメラ方向) → glTF RH Y-up に (x, -y, -z) で変換。
"""

from __future__ import annotations

import math

# ---- MediaPipe Pose ランドマーク index ----
NOSE = 0
L_SHOULDER, R_SHOULDER = 11, 12
L_ELBOW, R_ELBOW = 13, 14
L_WRIST, R_WRIST = 15, 16
L_HIP, R_HIP = 23, 24
L_KNEE, R_KNEE = 25, 26
L_ANKLE, R_ANKLE = 27, 28
L_TOE, R_TOE = 31, 32  # FOOT_INDEX

# 有効フレーム判定に使う主要ランドマーク
CORE_LANDMARKS = (L_SHOULDER, R_SHOULDER, L_HIP, R_HIP)
MIN_VISIBILITY = 0.5


# ============================================================
# ベクトル / クォータニオン ヘルパー (クォータニオンは (x, y, z, w))
# ============================================================
def v_add(a, b): return (a[0] + b[0], a[1] + b[1], a[2] + b[2])
def v_sub(a, b): return (a[0] - b[0], a[1] - b[1], a[2] - b[2])
def v_scale(a, s): return (a[0] * s, a[1] * s, a[2] * s)
def v_dot(a, b): return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]
def v_cross(a, b):
    return (a[1] * b[2] - a[2] * b[1],
            a[2] * b[0] - a[0] * b[2],
            a[0] * b[1] - a[1] * b[0])
def v_len(a): return math.sqrt(v_dot(a, a))
def v_mid(a, b): return v_scale(v_add(a, b), 0.5)
def v_lerp(a, b, t): return v_add(a, v_scale(v_sub(b, a), t))


def v_normalize(a, fallback=(0.0, 1.0, 0.0)):
    l = v_len(a)
    if l < 1e-8:
        return fallback
    return (a[0] / l, a[1] / l, a[2] / l)


Q_IDENTITY = (0.0, 0.0, 0.0, 1.0)


def q_mul(a, b):
    ax, ay, az, aw = a
    bx, by, bz, bw = b
    return (aw * bx + ax * bw + ay * bz - az * by,
            aw * by - ax * bz + ay * bw + az * bx,
            aw * bz + ax * by - ay * bx + az * bw,
            aw * bw - ax * bx - ay * by - az * bz)


def q_conj(q):
    return (-q[0], -q[1], -q[2], q[3])


def q_normalize(q):
    l = math.sqrt(q[0] ** 2 + q[1] ** 2 + q[2] ** 2 + q[3] ** 2)
    if l < 1e-8:
        return Q_IDENTITY
    return (q[0] / l, q[1] / l, q[2] / l, q[3] / l)


def q_from_two_vectors(a, b):
    """正規化済みベクトル a を b に重ねる最短回転"""
    d = v_dot(a, b)
    if d > 0.999999:
        return Q_IDENTITY
    if d < -0.999999:
        # 180°: a に垂直な任意軸
        axis = v_cross((1.0, 0.0, 0.0), a)
        if v_len(axis) < 1e-6:
            axis = v_cross((0.0, 1.0, 0.0), a)
        axis = v_normalize(axis)
        return (axis[0], axis[1], axis[2], 0.0)
    axis = v_cross(a, b)
    return q_normalize((axis[0], axis[1], axis[2], 1.0 + d))


def q_from_basis(x, y, z):
    """直交基底 (列ベクトル x, y, z) の回転行列をクォータニオンへ"""
    m00, m01, m02 = x[0], y[0], z[0]
    m10, m11, m12 = x[1], y[1], z[1]
    m20, m21, m22 = x[2], y[2], z[2]
    tr = m00 + m11 + m22
    if tr > 0.0:
        s = math.sqrt(tr + 1.0) * 2.0
        return q_normalize(((m21 - m12) / s, (m02 - m20) / s, (m10 - m01) / s, 0.25 * s))
    if m00 > m11 and m00 > m22:
        s = math.sqrt(1.0 + m00 - m11 - m22) * 2.0
        return q_normalize((0.25 * s, (m01 + m10) / s, (m02 + m20) / s, (m21 - m12) / s))
    if m11 > m22:
        s = math.sqrt(1.0 + m11 - m00 - m22) * 2.0
        return q_normalize(((m01 + m10) / s, 0.25 * s, (m12 + m21) / s, (m02 - m20) / s))
    s = math.sqrt(1.0 + m22 - m00 - m11) * 2.0
    return q_normalize(((m02 + m20) / s, (m12 + m21) / s, 0.25 * s, (m10 - m01) / s))


# ============================================================
# スケルトン定義
# ============================================================
# (name, parent_index)
JOINTS = [
    ("Hips", -1),            # 0
    ("Spine", 0),            # 1
    ("Chest", 1),            # 2
    ("Neck", 2),             # 3
    ("Head", 3),             # 4
    ("LeftClavicle", 2),     # 5
    ("LeftShoulder", 5),     # 6
    ("LeftElbow", 6),        # 7
    ("LeftWrist", 7),        # 8
    ("RightClavicle", 2),    # 9
    ("RightShoulder", 9),    # 10
    ("RightElbow", 10),      # 11
    ("RightWrist", 11),      # 12
    ("LeftUpLeg", 0),        # 13
    ("LeftKnee", 13),        # 14
    ("LeftAnkle", 14),       # 15
    ("LeftToe", 15),         # 16
    ("RightUpLeg", 0),       # 17
    ("RightKnee", 17),       # 18
    ("RightAnkle", 18),      # 19
    ("RightToe", 19),        # 20
]

# swing 回転を計算するときの主子ジョイント（Hips/Chest は基底方式、葉は単位のまま）
PRIMARY_CHILD = {
    1: 2,     # Spine → Chest
    3: 4,     # Neck → Head
    5: 6, 6: 7, 7: 8,       # LeftClavicle → Shoulder → Elbow → Wrist
    9: 10, 10: 11, 11: 12,  # RightClavicle → Shoulder → Elbow → Wrist
    13: 14, 14: 15, 15: 16,
    17: 18, 18: 19, 19: 20,
}

HIPS, SPINE, CHEST = 0, 1, 2


def _to_yup(p):
    """MediaPipe world → glTF RH Y-up"""
    return (p[0], -p[1], -p[2])


# MediaPipe には Neck / Clavicle ランドマークが無いため、既存点から内分して合成する。
NECK_LERP_T = 0.35       # Chest → NOSE 方向へどれだけ寄せるか（首の付け根の目安）
CLAVICLE_LERP_T = 0.3    # Chest → Shoulder 方向へどれだけ寄せるか（鎖骨の長さの目安）


def _joint_positions(lm):
    """33 ランドマーク（Y-up 変換済み）→ 21 ジョイントのグローバル位置"""
    hips = v_mid(lm[L_HIP], lm[R_HIP])
    chest = v_mid(lm[L_SHOULDER], lm[R_SHOULDER])
    spine = v_mid(hips, chest)
    neck = v_lerp(chest, lm[NOSE], NECK_LERP_T)
    l_clavicle = v_lerp(chest, lm[L_SHOULDER], CLAVICLE_LERP_T)
    r_clavicle = v_lerp(chest, lm[R_SHOULDER], CLAVICLE_LERP_T)
    return [
        hips, spine, chest, neck, lm[NOSE],
        l_clavicle, lm[L_SHOULDER], lm[L_ELBOW], lm[L_WRIST],
        r_clavicle, lm[R_SHOULDER], lm[R_ELBOW], lm[R_WRIST],
        lm[L_HIP], lm[L_KNEE], lm[L_ANKLE], lm[L_TOE],
        lm[R_HIP], lm[R_KNEE], lm[R_ANKLE], lm[R_TOE],
    ]


def _torso_basis(right_lm, left_lm, up_hint):
    """左右ランドマーク線 + 上方向ヒントから直交基底のクォータニオンを作る"""
    bx = v_normalize(v_sub(left_lm, right_lm), fallback=(1.0, 0.0, 0.0))
    bz = v_normalize(v_cross(bx, v_normalize(up_hint)), fallback=(0.0, 0.0, 1.0))
    by = v_cross(bz, bx)
    return q_from_basis(bx, by, bz)


def _frame_rotations(lm, pos, bind_pos, bind_hips_q, bind_chest_q):
    """1 フレーム分の全ジョイントのグローバル回転（バインド基準）を返す"""
    n = len(JOINTS)
    q_glob = [Q_IDENTITY] * n

    # Hips / Chest: 直交基底の相対回転
    hips_q = _torso_basis(lm[R_HIP], lm[L_HIP],
                          v_sub(v_mid(lm[L_SHOULDER], lm[R_SHOULDER]), pos[HIPS]))
    chest_q = _torso_basis(lm[R_SHOULDER], lm[L_SHOULDER],
                           v_sub(pos[CHEST], pos[HIPS]))
    q_glob[HIPS] = q_mul(hips_q, q_conj(bind_hips_q))
    q_glob[CHEST] = q_mul(chest_q, q_conj(bind_chest_q))

    # swing ジョイント: バインド方向 → 現在方向の最短回転
    for j, child in PRIMARY_CHILD.items():
        if j == CHEST:
            continue
        bind_dir = v_normalize(v_sub(bind_pos[child], bind_pos[j]))
        cur_dir = v_normalize(v_sub(pos[child], pos[j]))
        q_glob[j] = q_from_two_vectors(bind_dir, cur_dir)

    # 葉ジョイントは親のグローバル回転を継承（ローカル = 単位になる）
    for j, (_, parent) in enumerate(JOINTS):
        if j not in PRIMARY_CHILD and j not in (HIPS, CHEST):
            q_glob[j] = q_glob[parent] if parent >= 0 else Q_IDENTITY

    return q_glob


def _smooth_frames(frames, alpha=0.5):
    """ランドマーク位置に EMA を掛けてジッタを軽減する"""
    out = []
    prev = None
    for t, lm in frames:
        if prev is None:
            cur = list(lm)
        else:
            cur = [v_add(v_scale(p, alpha), v_scale(q, 1.0 - alpha))
                   for p, q in zip(lm, prev)]
        prev = cur
        out.append((t, cur))
    return out


def solve(frames, smoothing=True):
    """キャプチャフレーム列からスケルトン + キーフレームを構築する。

    frames: [(time_sec, [(x, y, z, visibility) x 33]), ...]  (MediaPipe world 座標)

    Returns dict:
        joints:          [{name, parent, bind_t(local), bind_p(global)}]
        times:           [t, ...]
        rot_keys:        {joint_name: [(t, (x,y,z,w)), ...]}  ローカル回転
        q_global_keys:   {joint_name: [(t, (x,y,z,w)), ...]}  グローバル回転（リターゲット用）
        root_trans_keys: [(t, (x,y,z)), ...]
        duration:        float
    """
    # 有効フレームだけ抽出し Y-up へ変換
    valid = []
    for t, lm in frames:
        if all(lm[i][3] >= MIN_VISIBILITY for i in CORE_LANDMARKS):
            valid.append((t, [_to_yup(p[:3]) for p in lm]))
    if len(valid) < 2:
        raise ValueError(f"有効フレームが不足しています ({len(valid)} フレーム)。"
                         "全身がカメラに写っているか確認してください。")

    # 時刻を 0 起点に
    t0 = valid[0][0]
    valid = [(t - t0, lm) for t, lm in valid]

    if smoothing:
        valid = _smooth_frames(valid)

    # バインドポーズ = 最初のフレーム
    bind_lm = valid[0][1]
    bind_pos = _joint_positions(bind_lm)
    bind_hips_q = _torso_basis(bind_lm[R_HIP], bind_lm[L_HIP],
                               v_sub(bind_pos[CHEST], bind_pos[HIPS]))
    bind_chest_q = _torso_basis(bind_lm[R_SHOULDER], bind_lm[L_SHOULDER],
                                v_sub(bind_pos[CHEST], bind_pos[HIPS]))

    joints = []
    for j, (name, parent) in enumerate(JOINTS):
        bind_t = bind_pos[j] if parent < 0 else v_sub(bind_pos[j], bind_pos[parent])
        joints.append({"name": name, "parent": parent,
                       "bind_t": bind_t, "bind_p": bind_pos[j]})

    times = []
    rot_keys = {name: [] for name, _ in JOINTS}
    q_global_keys = {name: [] for name, _ in JOINTS}
    root_trans_keys = []
    prev_q = [Q_IDENTITY] * len(JOINTS)

    for t, lm in valid:
        pos = _joint_positions(lm)
        q_glob = _frame_rotations(lm, pos, bind_pos, bind_hips_q, bind_chest_q)

        times.append(t)
        root_trans_keys.append((t, pos[HIPS]))
        for j, (name, _) in enumerate(JOINTS):
            q_global_keys[name].append((t, q_glob[j]))
        for j, (name, parent) in enumerate(JOINTS):
            if parent < 0:
                q_local = q_glob[j]
            else:
                q_local = q_mul(q_conj(q_glob[parent]), q_glob[j])
            q_local = q_normalize(q_local)
            # 符号の連続性を保つ（slerp の反転防止）
            if (q_local[0] * prev_q[j][0] + q_local[1] * prev_q[j][1] +
                    q_local[2] * prev_q[j][2] + q_local[3] * prev_q[j][3]) < 0.0:
                q_local = (-q_local[0], -q_local[1], -q_local[2], -q_local[3])
            prev_q[j] = q_local
            rot_keys[name].append((t, q_local))

    return {
        "joints": joints,
        "times": times,
        "rot_keys": rot_keys,
        "q_global_keys": q_global_keys,
        "root_trans_keys": root_trans_keys,
        "duration": times[-1],
    }
