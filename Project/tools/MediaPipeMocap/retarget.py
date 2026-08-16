"""キャプチャ済みモーションをターゲットモデルの骨格へリターゲットする。

方式（グローバル空間での回転差分の転写）:
  ソース骨格（このアプリの21ジョイント）はバインド回転が常に単位なので、
  キャプチャ済みグローバル回転 q_src_global がそのまま「バインドからの回転差分」になる。
  これをターゲットジョイントのバインドグローバル回転に左から合成する:

      q_tgt_animated_global[k] = q_src_global[j] * q_corr[k] * q_tgt_bind_global[k]

  その後、ターゲット自身の階層でローカル回転に戻す:

      q_tgt_animated_local[k] = conj(resolved_global[parent]) * q_tgt_animated_global[k]

  未マッピングのジョイントはバインドのローカル回転のまま（resolved_global には
  バインド値を合成して子の計算に使う）。指ボーンなどソースに対応が無い部位は
  自然に「バインドポーズで固定」になる。

ひねり補正 q_corr は「ボーン軸（バインド時のジョイント→最初の子の方向）まわりの
回転角（度）」として指定する。子が無いジョイントはグローバルY軸を使う。
"""

from __future__ import annotations

import math

from rig import (JOINTS, Q_IDENTITY, q_conj, q_from_basis, q_from_two_vectors,
                 q_mul, q_normalize, v_cross, v_len, v_normalize, v_sub)
from gltf_import import TargetModel

UNMAPPED = "— 未割当 —"
MAPPING_SUFFIX = ".retarget.json"

# 「名前から推測」用のヒント（前が優先。ends-with 一致 → 部分一致の順で探す）
GUESS_HINTS = {
    "Hips": ["hips", "pelvis"],
    "Spine": ["spine", "spine1", "spine_01"],
    "Chest": ["spine2", "spine_02", "chest", "upperchest"],
    "Neck": ["neck"],
    "Head": ["head"],
    "LeftClavicle": ["leftshoulder", "clavicle_l", "leftcollar", "shoulder_l"],
    "LeftShoulder": ["leftarm", "upperarm_l", "uparm_l", "arm_l"],
    "LeftElbow": ["leftforearm", "lowerarm_l", "forearm_l", "elbow_l"],
    "LeftWrist": ["lefthand", "hand_l", "wrist_l"],
    "RightClavicle": ["rightshoulder", "clavicle_r", "rightcollar", "shoulder_r"],
    "RightShoulder": ["rightarm", "upperarm_r", "uparm_r", "arm_r"],
    "RightElbow": ["rightforearm", "lowerarm_r", "forearm_r", "elbow_r"],
    "RightWrist": ["righthand", "hand_r", "wrist_r"],
    "LeftUpLeg": ["leftupleg", "thigh_l", "upleg_l"],
    "LeftKnee": ["leftleg", "calf_l", "shin_l", "knee_l"],
    "LeftAnkle": ["leftfoot", "foot_l", "ankle_l"],
    "LeftToe": ["lefttoebase", "toe_l", "ball_l"],
    "RightUpLeg": ["rightupleg", "thigh_r", "upleg_r"],
    "RightKnee": ["rightleg", "calf_r", "shin_r", "knee_r"],
    "RightAnkle": ["rightfoot", "foot_r", "ankle_r"],
    "RightToe": ["righttoebase", "toe_r", "ball_r"],
}


def _normalize(name: str) -> str:
    return name.lower().replace(":", "").replace("_", "").replace(" ", "")


def guess_mapping(target_names: list[str]) -> dict:
    """ヒント表からマッピング候補を作る（ユーザー確認前提の提案値）。

    target_names: ターゲット骨格のジョイント名一覧（glTFでもBlenderのボーン名でも可）。
    """
    normalized = [(_normalize(n), n) for n in target_names]
    used = set()
    result = {}
    for src, _ in JOINTS:
        hints = GUESS_HINTS.get(src, [])
        found = None
        for hint in hints:
            # ends-with 優先
            for norm, orig in normalized:
                if orig not in used and norm.endswith(hint):
                    found = orig
                    break
            if found:
                break
            for norm, orig in normalized:
                if orig not in used and hint in norm:
                    found = orig
                    break
            if found:
                break
        if found:
            result[src] = found
            used.add(found)
    return result


def _rotate_vec(q, v):
    qv = (v[0], v[1], v[2], 0.0)
    r = q_mul(q_mul(q, qv), q_conj(q))
    return (r[0], r[1], r[2])


def _axis_angle_quat(axis, degrees):
    if abs(degrees) < 1e-9:
        return Q_IDENTITY
    half = math.radians(degrees) * 0.5
    s = math.sin(half)
    a = v_normalize(axis)
    return q_normalize((a[0] * s, a[1] * s, a[2] * s, math.cos(half)))


def _bone_axis(target: TargetModel, k: int) -> tuple:
    """ひねり補正の回転軸 = バインド時のボーン方向（ジョイント→最初の子）"""
    children = [c for c in range(len(target.joint_names)) if target.parent[c] == k]
    if not children:
        return (0.0, 1.0, 0.0)
    p0 = target.bind_global_position(k)
    p1 = target.bind_global_position(children[0])
    d = v_sub(p1, p0)
    if v_len(d) < 1e-8:
        return (0.0, 1.0, 0.0)
    return v_normalize(d)


# ============================================================
# Tポーズ基準補正
# ============================================================
# ソースのバインドは「動画の最初のフレーム」なので、腕を下ろした姿勢などが
# そのままターゲットのバインド（普通はTポーズ）に対応してしまう。
# 各ソースジョイントについて「正準Tポーズ方向 → ソースバインド方向」の回転
# q_b2c を求め、q_src_global に右から合成することで、
# 「Tポーズからの回転」としてターゲットに転写できるようにする。
# （動画の最初のフレームが本当にTポーズなら q_b2c ≈ 単位で無害）

def _tpose_corrections(mocap: dict, arm_down_angle: float = 0.0) -> dict:
    """{ソースジョイント名: q_b2c} を返す。

    arm_down_angle: 正準ポーズの腕（上腕・前腕）を真横から下げる角度（度）。
        0 = Tポーズ、45前後 = Aポーズ（VRoid等）。
    """
    joints = mocap["joints"]
    pos = {j["name"]: j["bind_p"] for j in joints}

    # 被写体の左が +X か -X か（通常のカメラ正対キャプチャなら +X）
    if "LeftShoulder" in pos and "RightShoulder" in pos:
        left_sign = 1.0 if (pos["LeftShoulder"][0]
                            - pos["RightShoulder"][0]) >= 0 else -1.0
    else:
        left_sign = 1.0

    # 正準ポーズのボーン方向（swing ジョイント: 親→主子）
    UP, DOWN = (0.0, 1.0, 0.0), (0.0, -1.0, 0.0)
    L, R = (left_sign, 0.0, 0.0), (-left_sign, 0.0, 0.0)
    # 腕は指定角度だけ正面から見て下向きに傾ける（鎖骨はほぼ水平のままが一般的）
    rad = math.radians(arm_down_angle)
    LA = (math.cos(rad) * left_sign, -math.sin(rad), 0.0)
    RA = (-math.cos(rad) * left_sign, -math.sin(rad), 0.0)
    canonical_dirs = {
        "Spine": ("Chest", UP), "Neck": ("Head", UP),
        "LeftClavicle": ("LeftShoulder", L), "LeftShoulder": ("LeftElbow", LA),
        "LeftElbow": ("LeftWrist", LA),
        "RightClavicle": ("RightShoulder", R), "RightShoulder": ("RightElbow", RA),
        "RightElbow": ("RightWrist", RA),
        "LeftUpLeg": ("LeftKnee", DOWN), "LeftKnee": ("LeftAnkle", DOWN),
        "RightUpLeg": ("RightKnee", DOWN), "RightKnee": ("RightAnkle", DOWN),
        # つま先は水平投影したバインド方向を正準とする（向き規約に依存しない）
        "LeftAnkle": ("LeftToe", None), "RightAnkle": ("RightToe", None),
    }

    def torso_basis(right_p, left_p, up_hint):
        bx = v_normalize(v_sub(left_p, right_p), fallback=(1.0, 0.0, 0.0))
        bz = v_normalize(v_cross(bx, v_normalize(up_hint)),
                         fallback=(0.0, 0.0, 1.0))
        by = v_cross(bz, bx)
        return q_from_basis(bx, by, bz)

    corrections = {}

    # Hips / Chest: 基底同士の差
    canon_basis = q_from_basis((left_sign, 0.0, 0.0), (0.0, 1.0, 0.0),
                               (0.0, 0.0, left_sign))
    if all(n in pos for n in ("Chest", "Hips", "RightUpLeg", "LeftUpLeg",
                              "RightShoulder", "LeftShoulder")):
        up_hint = v_sub(pos["Chest"], pos["Hips"])
        bind_hips = torso_basis(pos["RightUpLeg"], pos["LeftUpLeg"], up_hint)
        bind_chest = torso_basis(pos["RightShoulder"], pos["LeftShoulder"],
                                 up_hint)
        corrections["Hips"] = q_normalize(q_mul(bind_hips, q_conj(canon_basis)))
        corrections["Chest"] = q_normalize(q_mul(bind_chest,
                                                 q_conj(canon_basis)))

    # swing ジョイント: 正準方向 → バインド方向 の最短回転
    for name, (child, canon) in canonical_dirs.items():
        if name not in pos or child not in pos:
            continue
        bind_dir = v_normalize(v_sub(pos[child], pos[name]))
        if canon is None:
            canon = v_normalize((bind_dir[0], 0.0, bind_dir[2]),
                                fallback=(0.0, 0.0, 1.0))
        corrections[name] = q_from_two_vectors(canon, bind_dir)

    # 葉ジョイントは親の補正を継承（q_global 自体が親の値なので整合する）
    for j in joints:
        if j["name"] not in corrections:
            parent = j["parent"]
            parent_name = joints[parent]["name"] if parent >= 0 else None
            corrections[j["name"]] = corrections.get(parent_name, Q_IDENTITY)

    return corrections


def compute_auto_scale(mocap: dict, target: TargetModel, mapping: dict) -> float:
    """ルート移動量のスケール係数を Hips↔Head 間の距離比から自動算出する。
    どちらかが未マッピングなら 1.0。"""
    tgt_hips = mapping.get("Hips")
    tgt_head = mapping.get("Head")
    if not tgt_hips or not tgt_head:
        return 1.0

    src_by_name = {j["name"]: j for j in mocap["joints"]}
    src_len = v_len(v_sub(src_by_name["Head"]["bind_p"],
                          src_by_name["Hips"]["bind_p"]))
    tgt_len = v_len(v_sub(
        target.bind_global_position(target.joint_index(tgt_head)),
        target.bind_global_position(target.joint_index(tgt_hips))))
    if src_len < 1e-8:
        return 1.0
    return tgt_len / src_len


def retarget(mocap: dict, target: TargetModel, mapping: dict,
             corrections: dict | None = None,
             root_scale: float | None = None,
             tpose_reference: bool = True,
             arm_down_angle: float = 0.0) -> dict:
    """リターゲットを実行し、エクスポータに渡せる結果 dict を返す。

    mocap:       rig.solve() / capture_project.load() の結果
    target:      gltf_import.load_gltf() で読んだターゲットモデル
    mapping:     {ソースジョイント名: ターゲットジョイント名}（未割当は含めない）
    corrections: {ターゲットジョイント名: ひねり補正角(度)}
    root_scale:  ルート移動のスケール係数（None なら自動算出）
    tpose_reference: True なら「正準ポーズからの回転」として転写する
        （ターゲットのバインドが正準ポーズの場合に正しい対応になる。
         動画の最初のフレームが腕を下ろした姿勢でもオフセットが乗らない）
    arm_down_angle: 正準ポーズの腕を真横から下げる角度（度）。
        0 = Tポーズバインドのモデル向け、45前後 = Aポーズ（VRoid等）向け。
        tpose_reference=False のときは無視される。

    Returns dict:
        joint_names, parent, bind_local_t/r/s: ターゲット骨格の情報
        times, duration
        rot_local_keys: {tgt_name: [(t, quat)]}  マッピング済みジョイントのみ
        root_trans_keys: [(t, vec3)] or None     ルート（Hips対応先）のローカル並進
        root_joint: str or None
        root_scale: 使用したスケール係数
    """
    corrections = corrections or {}
    if root_scale is None:
        root_scale = compute_auto_scale(mocap, target, mapping)

    n = len(target.joint_names)
    times = mocap["times"]
    q_global_keys = mocap["q_global_keys"]
    q_b2c = (_tpose_corrections(mocap, arm_down_angle)
             if tpose_reference else {})

    # ターゲットジョイント index → ソースジョイント名
    tgt_to_src: dict[int, str] = {}
    for src_name, tgt_name in mapping.items():
        if not tgt_name:
            continue
        if tgt_name not in target.joint_names:
            raise ValueError(f"ターゲットにジョイント '{tgt_name}' がありません")
        tgt_to_src[target.joint_index(tgt_name)] = src_name

    # ひねり補正クォータニオンを事前計算
    q_corr = [Q_IDENTITY] * n
    for tgt_name, degrees in corrections.items():
        if tgt_name in target.joint_names:
            k = target.joint_index(tgt_name)
            q_corr[k] = _axis_angle_quat(_bone_axis(target, k), degrees)

    rot_local_keys: dict[str, list] = {target.joint_names[k]: []
                                       for k in tgt_to_src}
    prev_q: dict[int, tuple] = {}

    for fi, t in enumerate(times):
        resolved_global = [Q_IDENTITY] * n
        for k in range(n):  # 階層順（親が先）保証済み
            parent = target.parent[k]
            parent_g = resolved_global[parent] if parent >= 0 else Q_IDENTITY
            if k in tgt_to_src:
                src_name = tgt_to_src[k]
                q_src = q_global_keys[src_name][fi][1]
                if tpose_reference:
                    # 「Tポーズからの回転」に変換してから転写
                    q_src = q_mul(q_src, q_b2c.get(src_name, Q_IDENTITY))
                g = q_mul(q_mul(q_src, q_corr[k]), target.bind_global_r[k])
                resolved_global[k] = q_normalize(g)
                q_local = q_normalize(q_mul(q_conj(parent_g), resolved_global[k]))
                # 符号の連続性（slerp反転防止）
                pq = prev_q.get(k)
                if pq is not None and (q_local[0] * pq[0] + q_local[1] * pq[1] +
                                       q_local[2] * pq[2] + q_local[3] * pq[3]) < 0.0:
                    q_local = (-q_local[0], -q_local[1], -q_local[2], -q_local[3])
                prev_q[k] = q_local
                rot_local_keys[target.joint_names[k]].append((t, q_local))
            else:
                resolved_global[k] = q_normalize(
                    q_mul(parent_g, target.bind_local_r[k]))

    # ルート並進: ソースHipsの移動量をスケールしてターゲットのローカル並進へ
    root_joint = mapping.get("Hips")
    root_trans_keys = None
    if root_joint and root_joint in target.joint_names:
        k = target.joint_index(root_joint)
        parent = target.parent[k]
        # 親のバインドグローバル回転の逆で、ワールド移動量を親ローカル空間へ
        parent_bind_g = target.bind_global_r[parent] if parent >= 0 else Q_IDENTITY
        inv_parent = q_conj(parent_bind_g)
        src_bind_pos = next(j["bind_p"] for j in mocap["joints"]
                            if j["name"] == "Hips")
        bind_t = target.bind_local_t[k]
        root_trans_keys = []
        for t, pos in mocap["root_trans_keys"]:
            delta_world = (
                (pos[0] - src_bind_pos[0]) * root_scale,
                (pos[1] - src_bind_pos[1]) * root_scale,
                (pos[2] - src_bind_pos[2]) * root_scale)
            d_local = _rotate_vec(inv_parent, delta_world)
            root_trans_keys.append((t, (bind_t[0] + d_local[0],
                                        bind_t[1] + d_local[1],
                                        bind_t[2] + d_local[2])))

    return {
        "joint_names": list(target.joint_names),
        "parent": list(target.parent),
        "bind_local_t": list(target.bind_local_t),
        "bind_local_r": list(target.bind_local_r),
        "bind_local_s": list(target.bind_local_s),
        "times": list(times),
        "duration": mocap["duration"],
        "rot_local_keys": rot_local_keys,
        "root_trans_keys": root_trans_keys,
        "root_joint": root_joint,
        "root_scale": root_scale,
    }
