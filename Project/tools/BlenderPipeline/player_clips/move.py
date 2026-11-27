"""まとまり move（B 移動・回避）：STG 立ち姿勢（ループ）、STG 滑空・前へ倒れた姿勢（ループ、8 番 §2.2.1）、
ボス戦の待機（ループ）、走り（前後左右）、回避（STG・地上）。

鳴らすのは PlayerAnimPart（Resources/Json/Tuning/PlayerAnim/move.json の railStand / railGlide / bossIdle /
bossRunForward・Back・Left・Right / railDodgeLeft・Right・Up・Down / groundDodgeForward・Back・Left・Right）。
書き方は 25_PlayerAnimClips.md §3。

2026-11-27 ユーザー決定：
  ・地上の走りは足を運ばない（地上の速さ 12 m/s では足を運ぶと足がすべって見える）。→ 同日見直し：浮かせず、
    両足を床に着けたままひざを曲げて地面をスライドする。左右はスキーのターン（両足を着けたままひざを曲がる側へ入れ、
    上体を曲がる側へ傾ける）。ゲームは入力の向きへ自機を回すので、回っている間は左右、向きがそろうと前が鳴る
  ・地上の回避も空中と同じ一回転にし、腰を弧を描くように持ち上げる（同日ユーザー）
  ・待機は直立に近く（BossStandIdle）。ほかの動きへ移るときにひざを曲げるのが予兆になる。すべての動きに
    ゲーム画面でわかる大げさな予兆を入れる（回避は出だしでしゃがむ・逆へひねる・縮む）（同日ユーザー）
  ・後ろのスライドは作らない：地上の自機は入力の向きへ回る（StagePlayScene::UpdateBossPlayerFacing）ので、
    真後ろへ進むのは反転し始めの一瞬だけ。bossRunBack は前のスライドで代用する（同日ユーザー）
  ・姿勢の BossIdle（腰を落とした構え）は近接（melee.py）・射撃（shoot.py）が元の姿勢に使うので変えない。
    待機のクリップ BossIdle だけ BossStandIdle にした
  ・地上の前の回避：前へ飛び込んできりもみで一回転→着地して地面で前転。地上の後ろの回避：しなるバック宙
    （右ひざは少し曲げ・左脚は伸ばす）。空中の下：予兆なしでまっすぐ突っ込む（同日ユーザー）。
    前と後ろは 0.4 秒では読めないので回避のクールダウン（dodge.cooldown＝0.6 秒）に合わせる
  ・STG の回避の左右は横に一回転（バレルロール）。上下は体を丸める／前へ突っ込む
  ・STG の立ち姿勢は起こした立ち（前へ約 15°）。滑空は約 70° 前へ倒す（正面の当たり判定を小さくする意図）
回避の長さはゲームの回避の無敵時間（StagePlay.json の dodge.iframeDuration＝0.4 秒）に合わせる。
羽根（STG では展開）は Spine2 に付くので、腕は背中へ回さない（体の横か前）。
"""

import math

# 回避の長さ [秒]（StagePlay.json の dodge.iframeDuration）
DODGE_DURATION = 0.4
# スライド移動のループの長さ [秒]（小さな揺れの周期）
SLIDE_LOOP = 1.0
# 地上の回避の一回転で腰を持ち上げるいちばん高い所 [m]（弧を描く）
GROUND_ROLL_LIFT = 0.35
# 一回転のあいだに脚を抱える角度 [度]（太ももを前へ上げる・すねを後ろへたたむ）
ROLL_TUCK_THIGH = 35.0
ROLL_TUCK_SHIN = 40.0
# 一回転の角度 [度]
ROLL_TURN = 360.0
# 回避の予兆：回避の長さのうち予兆に使う割合と、逆へひねる角度 [度]・地上でしゃがむ深さ [m]
ROLL_ANTIC = 0.25
ROLL_COUNTER = 25.0
GROUND_ROLL_CROUCH = 0.2
# 回転が進んでから脚を伸ばし始める位置（回転の 0〜1 のうち）
ROLL_UNTUCK_FROM = 0.5
# しゃがみを回転の始めのうちに戻し切る位置（回転の 0〜1 のうち）
ROLL_CROUCH_RELEASE = 0.3
# 地上の回避で、しゃがむときにひざを出す向き（右側の書き方）
GROUND_ROLL_KNEE = (-0.05, -1.0, 0.0)
# 地上の前・後ろの回避の長さ [秒]（StagePlay.json の dodge.cooldown。次の回避より先に終わる）
GROUND_FLIP_DURATION = 0.6
# 地上の前転・バック宙：体を丸めるときの脚（太ももを上げる・すねをたたむ [度]）と背中・首の丸め（合計 [度]）
FLIP_TUCK_THIGH = 95.0
FLIP_TUCK_SHIN = 110.0
FLIP_CURL = 50.0
# 背骨・首の反り／丸めを各ボーンに分ける割合（合計 1）
SPINE_SHARE = {"Spine": 0.25, "Spine1": 0.25, "Spine2": 0.2, "Neck": 0.15, "Head": 0.15}
# バック宙の脚：右の太ももを前へ上げる・右のすねをたたむ・左の太ももを後ろへ流す [度]
FLIP_SPLIT_RIGHT_THIGH = 30.0
FLIP_SPLIT_RIGHT_SHIN = 70.0
FLIP_SPLIT_LEFT_THIGH = 12.0
# 上下の回避のキーの位置（回避の長さのうちの割合）：予兆・いちばん大きい所・そこを保つ所
DODGE_KEY_ANTIC = 0.2
DODGE_KEY_PEAK = 0.5
DODGE_KEY_HOLD = 0.75

# 向き（アーマチュアの空間：正面 −Y、+X がモデルの左、+Z が上）。体を傾ける軸
AXIS_FORWARD_LEAN = (1.0, 0.0, 0.0)   # 正の角度で前へ倒れる
AXIS_SIDE_LEAN = (0.0, 1.0, 0.0)      # 正の角度でモデルの左（+X）へ倒れる・左へ回る
AXIS_TWIST = (0.0, 0.0, 1.0)          # 正の角度で正面がモデルの左（+X）へ向く


def _rotate_hips_about(arm, rig, pivot, axis, deg):
    """腰（体全体）を pivot のまわりに axis で deg 度回す"""
    from mathutils import Matrix, Quaternion, Vector
    hips = arm.pose.bones[rig.MX + "Hips"]
    rot = Quaternion(Vector(axis).normalized(), math.radians(deg)).to_matrix().to_4x4()
    hips.matrix = Matrix.Translation(pivot) @ rot @ Matrix.Translation(-pivot) @ hips.matrix
    import bpy
    bpy.context.view_layer.update()


def _ski_knees(arm, rig, knees):
    """スキーのターンの脚：足首は床のまま（立ちの位置）、ひざを左右別の向きへ入れ直す。
    knees＝{"Right": 向き, "Left": 向き}（どちらも右側の書き方。左は x を反転して使われる）"""
    for side in ("Right", "Left"):
        rig.leg_ik(arm, side, rig.body_ankle_target(arm, side), knees[side])
        rig.foot_flat(arm, side, 0.0)


def ski_call(knees):
    """姿勢の call に入れる関数（スキーのターンのひざ）"""
    return lambda arm, rig, phase: _ski_knees(arm, rig, knees)


def _ease_out_cubic(u):
    return 1.0 - (1.0 - u) ** 3


def _smooth(u):
    u = min(max(u, 0.0), 1.0)
    return u * u * (3.0 - 2.0 * u)


def _roll(arm, rig, phase, sign, lift=0.0, crouch=0.0):
    """回避の一回転。sign：+1 で左、−1 で右。
    予兆（phase 0〜ROLL_ANTIC）：脚を抱え、逆へ ROLL_COUNTER 度ひねり、地上は crouch [m] しゃがむ。
    回転：胸（Spine1）を中心に進む向きの軸で、逆へひねった所から一回転して戻る。lift：腰を持ち上げるいちばん高い所 [m]
    （地上の回避で弧を描く。回転の半ばでいちばん高い）"""
    from mathutils import Matrix, Quaternion, Vector
    import bpy
    hips = arm.pose.bones[rig.MX + "Hips"]
    if phase < ROLL_ANTIC:
        w = _smooth(phase / ROLL_ANTIC)
        tuck = w
        sink = w
        angle = -ROLL_COUNTER * w
        rise = 0.0
    else:
        q = (phase - ROLL_ANTIC) / (1.0 - ROLL_ANTIC)
        tuck = 1.0 - _smooth((q - ROLL_UNTUCK_FROM) / (1.0 - ROLL_UNTUCK_FROM))
        sink = 1.0 - _smooth(q / ROLL_CROUCH_RELEASE)
        angle = -ROLL_COUNTER + (ROLL_TURN + ROLL_COUNTER) * _ease_out_cubic(q)
        rise = lift * math.sin(math.pi * q)
    if crouch:
        # しゃがむ：腰を下げて、足首は床のまま脚を曲げ直す
        hips.matrix = Matrix.Translation((0.0, 0.0, -crouch * sink)) @ hips.matrix
        bpy.context.view_layer.update()
        for side in ("Right", "Left"):
            rig.leg_ik(arm, side, rig.body_ankle_target(arm, side), GROUND_ROLL_KNEE)
            rig.foot_flat(arm, side, 0.0)
    for side in ("Right", "Left"):
        rig.rotate_pose_bone(arm, rig.MX + side + "UpLeg",
                             Quaternion(Vector(AXIS_FORWARD_LEAN), math.radians(-ROLL_TUCK_THIGH * tuck)))
        rig.rotate_pose_bone(arm, rig.MX + side + "Leg",
                             Quaternion(Vector(AXIS_FORWARD_LEAN), math.radians(ROLL_TUCK_SHIN * tuck)))
    pivot = arm.pose.bones[rig.MX + "Spine1"].head.copy()
    _rotate_hips_about(arm, rig, pivot, AXIS_SIDE_LEAN, sign * angle)
    if rise:
        hips.matrix = Matrix.Translation((0.0, 0.0, rise)) @ hips.matrix
        bpy.context.view_layer.update()


def roll_call(sign, lift=0.0, crouch=0.0):
    """姿勢の call に入れる関数（一回転）"""
    return lambda arm, rig, phase: _roll(arm, rig, phase, sign, lift, crouch)


def _track(keys, phase):
    """[(phase, 値[, つなぎ方]), …] の phase の値。つなぎ方は smooth（既定）/ linear / in / out"""
    if phase <= keys[0][0]:
        return keys[0][1]
    for k0, k1 in zip(keys, keys[1:]):
        if phase <= k1[0]:
            u = (phase - k0[0]) / max(k1[0] - k0[0], 1e-6)
            kind = k1[2] if len(k1) > 2 else "smooth"
            if kind == "in":
                u = u * u
            elif kind == "out":
                u = 1.0 - (1.0 - u) ** 2
            elif kind != "linear":
                u = _smooth(u)
            return k0[1] + (k1[1] - k0[1]) * u
    return keys[-1][1]


def _arm_track(keys, phase):
    """[(phase, Arm の向き, ForeArm の向き), …] の phase の向き（右側の書き方。あいだは向きを混ぜて正規化）"""
    from mathutils import Vector
    if phase <= keys[0][0]:
        return keys[0][1], keys[0][2]
    for k0, k1 in zip(keys, keys[1:]):
        if phase <= k1[0]:
            u = _smooth((phase - k0[0]) / max(k1[0] - k0[0], 1e-6))
            return tuple(Vector(a).normalized().lerp(Vector(b).normalized(), u)
                         for a, b in ((k0[1], k1[1]), (k0[2], k1[2])))
    return keys[-1][1], keys[-1][2]


def _acro(arm, rig, phase, spec):
    """地上の回避の大技（前転・バック宙）。spec の各トラック（phase の関数）で体を組む：
    crouch＝しゃがむ深さ [m]、arch＝背中・首の反り（合計 [度]、負で反る）、tuck＝体を丸める割合、
    split＝バック宙の脚の割合、arms＝腕の向き、pitch＝体全体を前へ回す [度]、spin＝きりもみ [度]、
    clear＝体のいちばん低い所の床からの高さ [m]（毎コマ床に合わせる＝地面を転がる・弧を描いて跳ぶ）。
    回すのは胸（Spine1）を中心に、前へ回す→きりもみ（進む向きの軸）の順"""
    from mathutils import Matrix, Quaternion, Vector
    import bpy
    x_axis = Vector(AXIS_FORWARD_LEAN)
    hips = arm.pose.bones[rig.MX + "Hips"]
    drop = _track(spec["crouch"], phase)
    hips.matrix = Matrix.Translation((0.0, 0.0, -drop)) @ hips.matrix
    bpy.context.view_layer.update()
    for side in ("Right", "Left"):
        rig.leg_ik(arm, side, rig.body_ankle_target(arm, side), GROUND_ROLL_KNEE)
        rig.foot_flat(arm, side, 0.0)
    tuck = _track(spec["tuck"], phase)
    spine_deg = _track(spec["arch"], phase) + FLIP_CURL * tuck
    for bone, share in SPINE_SHARE.items():
        rig.rotate_pose_bone(arm, rig.MX + bone, Quaternion(x_axis, math.radians(spine_deg * share)))
    upper, fore = _arm_track(spec["arms"], phase)
    for side in ("Right", "Left"):
        rig.pose_bone_toward(arm, rig.MX + side + "Arm", rig.side_vec(upper, side))
        rig.pose_bone_toward(arm, rig.MX + side + "ForeArm", rig.side_vec(fore, side))
    split = _track(spec["split"], phase)
    leg_deg = {"RightUpLeg": -FLIP_TUCK_THIGH * tuck - FLIP_SPLIT_RIGHT_THIGH * split,
               "RightLeg": FLIP_TUCK_SHIN * tuck + FLIP_SPLIT_RIGHT_SHIN * split,
               "LeftUpLeg": -FLIP_TUCK_THIGH * tuck + FLIP_SPLIT_LEFT_THIGH * split,
               "LeftLeg": FLIP_TUCK_SHIN * tuck}
    for bone, deg in leg_deg.items():
        rig.rotate_pose_bone(arm, rig.MX + bone, Quaternion(x_axis, math.radians(deg)))
    pivot = arm.pose.bones[rig.MX + "Spine1"].head.copy()
    rot = (Quaternion(Vector(AXIS_SIDE_LEAN), math.radians(_track(spec["spin"], phase)))
           @ Quaternion(x_axis, math.radians(_track(spec["pitch"], phase))))
    hips.matrix = Matrix.Translation(pivot) @ rot.to_matrix().to_4x4() @ Matrix.Translation(-pivot) @ hips.matrix
    bpy.context.view_layer.update()
    low = rig.posed_min_z(rig.BODY_OBJECT[0])
    hips.matrix = Matrix.Translation((0.0, 0.0, rig.FLOOR_Z[0] + _track(spec["clear"], phase) - low)) @ hips.matrix
    bpy.context.view_layer.update()


def acro_call(spec):
    """姿勢の call に入れる関数（地上の回避の大技）"""
    return lambda arm, rig, phase: _acro(arm, rig, phase, spec)


# 待機（BossStandIdle）の腕の向き（大技の始まりと終わり）
IDLE_ARM = (-0.22, -0.05, -1.0)
IDLE_FORE = (-0.05, -0.4, -1.0)
# 予兆で腕を後ろへ引く
ARM_SWING_BACK = ((-0.25, 0.7, -0.7), (-0.2, 0.8, -0.5))

# 地上の前の回避（phase は 0〜1。0.6 秒）：
#   0〜0.12 しゃがんで腕を後ろへ引く → 〜0.45 前へ飛び込み、体を水平にしながらきりもみで一回転（腕は前へ伸ばす。
#   弧を描いて 0.45m 浮く）→ 0.45 着地 → 〜0.85 ひざを抱えて地面で前転 → 〜1 立ち上がる
FLIP_FORWARD = {
    "crouch": [(0.0, 0.0), (0.12, 0.2), (0.22, 0.0), (0.78, 0.0), (0.88, 0.25), (1.0, 0.0)],
    "arch": [(0.0, 0.0), (0.12, 10.0), (0.3, -10.0), (0.45, 0.0)],
    "tuck": [(0.0, 0.0), (0.45, 0.0), (0.52, 1.0), (0.78, 1.0), (0.9, 0.0)],
    "split": [(0.0, 0.0)],
    "arms": [(0.0, IDLE_ARM, IDLE_FORE), (0.12, *ARM_SWING_BACK),
             (0.3, (-0.15, -0.25, 1.0), (-0.1, -0.25, 1.0)), (0.45, (-0.15, -0.25, 1.0), (-0.1, -0.25, 1.0)),
             (0.55, (-0.35, -0.7, -0.4), (0.4, -0.6, 0.2)), (0.82, (-0.35, -0.7, -0.4), (0.4, -0.6, 0.2)),
             (1.0, IDLE_ARM, IDLE_FORE)],
    "pitch": [(0.0, 0.0), (0.12, 20.0), (0.45, 85.0, "out"), (0.85, 360.0), (1.0, 360.0)],
    "spin": [(0.0, 0.0), (0.12, 0.0), (0.45, 360.0)],
    "clear": [(0.0, 0.0), (0.12, 0.0), (0.28, 0.45, "out"), (0.45, 0.02, "in"), (1.0, 0.0)],
}
# 地上の後ろの回避（0.6 秒）：しなるバック宙
#   0〜0.12 しゃがんで腕を下げる → 〜0.8 跳んで背中を大きく反らせ（頭も後ろへ）、後ろへ一回転（右ひざは少し曲げ・
#   左脚は伸ばす。腕は頭の上から横へ振る。0.6m 浮く）→ 0.8〜0.86 しゃがんで着地 → 〜1 立ち上がる
FLIP_BACK = {
    "crouch": [(0.0, 0.0), (0.12, 0.22), (0.2, 0.0), (0.78, 0.0), (0.86, 0.2), (1.0, 0.0)],
    "arch": [(0.0, 0.0), (0.12, 8.0), (0.35, -60.0), (0.62, -40.0), (0.8, 0.0)],
    "tuck": [(0.0, 0.0)],
    "split": [(0.0, 0.0), (0.15, 0.0), (0.3, 1.0), (0.7, 1.0), (0.82, 0.0)],
    "arms": [(0.0, IDLE_ARM, IDLE_FORE), (0.12, *ARM_SWING_BACK),
             (0.35, (-0.3, 0.3, 1.0), (-0.3, 0.4, 1.0)), (0.6, (-1.0, 0.2, 0.2), (-1.0, 0.1, 0.0)),
             (0.85, (-0.4, -0.6, -0.6), (-0.2, -0.9, -0.2)), (1.0, IDLE_ARM, IDLE_FORE)],
    "pitch": [(0.0, 0.0), (0.12, 10.0), (0.8, -360.0), (1.0, -360.0)],
    "spin": [(0.0, 0.0)],
    "clear": [(0.0, 0.0), (0.12, 0.0), (0.45, 0.6, "out"), (0.8, 0.0, "in"), (1.0, 0.0)],
}


# スキーのターンのひざ（右側の書き方。x が正＝モデルの左（+X）へ。左の脚は x を反転して使われるので、
# 左へ曲がる（ひざを +X へ入れる）ときは右の脚に +、左の脚に − を書く）
SKI_KNEES_LEFT = {"Right": (1.2, -1.0, 0.0), "Left": (-1.2, -1.0, 0.0)}
SKI_KNEES_RIGHT = {"Right": (-1.2, -1.0, 0.0), "Left": (1.2, -1.0, 0.0)}
# STG の脚（空中。ひざを軽く曲げて下げ、左を少し後ろへ流す）
RAIL_LEGS = {"ik": False,
             "Right": {"UpLeg": (-0.05, -0.12, -1.0), "Leg": (0.0, 0.3, -1.0), "Foot": (0.0, 0.1, -1.0)},
             "Left": {"UpLeg": (-0.05, 0.1, -1.0), "Leg": (0.0, 0.55, -1.0), "Foot": (0.0, 0.4, -1.0)}}
# 滑空の脚：そろえて後ろへまっすぐ伸ばし、つま先も後ろへ
GLIDE_LEGS = {"ik": False,
              "Right": {"UpLeg": (-0.04, 0.12, -1.0), "Leg": (0.0, 0.22, -1.0), "Foot": (0.0, 0.9, -0.6)},
              "Left": {"UpLeg": (-0.04, 0.2, -1.0), "Leg": (0.0, 0.35, -1.0), "Foot": (0.0, 0.9, -0.5)}}

POSES = {
    # ボス戦の待機（2026-11-27、土台の流れを通すお試しの 1 本。実機で鳴ったのでそのまま使う）：
    # 直立（start の Stand）から腰を少し落とし、ひざを曲げ、腕を体の前へ軽く上げて構える
    "BossIdle": {
        "base": "Stand",
        "hips": {"offset": (0.0, 0.02, -0.08), "lean": 6.0},
        "bend": {"Spine": 4.0, "Spine1": 4.0, "Spine2": 2.0, "Neck": -6.0, "Head": -4.0},
        "arms": {"Arm": (-0.3, -0.12, -1.0), "ForeArm": (0.05, -1.0, 0.15)},
        "legs": {"ik": True, "knee": (-0.2, -1.0, 0.0), "toe_out": 0.0},
    },

    # 待機（クリップ BossIdle が使う。2026-11-27 ユーザー：直立に近く。ほかの動きへ移るときにひざを曲げるのが予兆になる）：
    # 直立（start の Stand）から腰をわずかに落とし、腕は体の横で少し曲げる
    "BossStandIdle": {
        "base": "Stand",
        "hips": {"offset": (0.0, 0.0, -0.04), "lean": 2.0},
        "bend": {"Spine": 4.0, "Spine1": 3.0, "Spine2": 1.0, "Neck": -3.0, "Head": -2.0},
        "arms": {"Arm": (-0.22, -0.05, -1.0), "ForeArm": (-0.05, -0.4, -1.0)},
        "legs": {"ik": True, "knee": (-0.1, -1.0, 0.0), "toe_out": 0.0},
    },

    # ---- STG ----
    # 立ち姿勢（射撃中・滑空していない時）：体を少し前へ（約 15°）、脚は軽く曲げて下げる。腕は体の横で前腕を前へ
    "RailStand": {
        "tilt": 10.0,
        "hips": {"offset": (0.0, 0.0, 0.0), "lean": 3.0},
        "bend": {"Spine": 1.0, "Spine1": 1.0, "Spine2": 1.0, "Neck": -6.0, "Head": -5.0},
        "arms": {"Arm": (-0.35, 0.05, -1.0), "ForeArm": (-0.15, -0.45, -1.0)},
        "legs": RAIL_LEGS,
        "sway": {"spine": 0.8, "hips_z": 0.01, "arm": 2.0, "leg": 3.0},
    },
    # 滑空（チャージ 1 段目が溜まった後）：体全体を約 70° 前へ倒し、顔は前を見る。腕は体に沿わせて後ろへ（背中の羽根に
    # 当たらないよう体の横を通す）、脚はそろえて後ろへ伸ばす
    "RailGlide": {
        "tilt": 58.0,
        "hips": {"offset": (0.0, 0.0, 0.0), "lean": 6.0},
        "bend": {"Spine": 3.0, "Spine1": 3.0, "Spine2": 2.0, "Neck": -30.0, "Head": -28.0},
        "arms": {"Arm": (-0.3, 0.35, -1.0), "ForeArm": (-0.2, 0.45, -1.0)},
        "legs": GLIDE_LEGS,
        "sway": {"spine": 0.6, "hips_z": 0.012, "arm": 1.5, "leg": 2.0},
    },
    # 回避（左右）：立ち姿勢のまま脚を抱えて一回転（phase で回す）
    "RailRollLeft": {"base": "RailStand", "sway": {}, "call": roll_call(1.0)},
    "RailRollRight": {"base": "RailStand", "sway": {}, "call": roll_call(-1.0)},
    # 回避（上）：体を反らせて脚を抱え、腕を横へ開く
    "RailDodgeUpPose": {
        "base": "RailStand",
        "tilt": -5.0,
        "hips": {"offset": (0.0, 0.0, 0.05), "lean": 0.0},
        "bend": {"Spine": -6.0, "Spine1": -6.0, "Spine2": -4.0, "Neck": -4.0, "Head": -4.0},
        "arms": {"Arm": (-0.8, 0.0, -0.45), "ForeArm": (-0.7, -0.3, -0.5)},
        "legs": {"ik": False,
                 "Right": {"UpLeg": (-0.1, -0.8, -0.6), "Leg": (0.0, 0.4, -1.0), "Foot": (0.0, 0.2, -1.0)},
                 "Left": {"UpLeg": (-0.1, -0.7, -0.7), "Leg": (0.0, 0.5, -1.0), "Foot": (0.0, 0.3, -1.0)}},
        "sway": {},
    },
    # 回避（上）の予兆：一度縮む（体を丸めて腰を沈め、腕を体に寄せる）
    "RailDodgeUpAntic": {
        "base": "RailStand",
        "tilt": 20.0,
        "hips": {"offset": (0.0, 0.0, -0.08), "lean": 6.0},
        "bend": {"Spine": 10.0, "Spine1": 10.0, "Spine2": 6.0, "Neck": -20.0, "Head": -14.0},
        "arms": {"Arm": (-0.15, -0.3, -0.95), "ForeArm": (0.3, -0.4, 0.85)},
        "legs": {"ik": False,
                 "Right": {"UpLeg": (-0.05, -0.6, -0.8), "Leg": (0.0, 0.8, -0.6), "Foot": (0.0, 0.3, -1.0)},
                 "Left": {"UpLeg": (-0.05, -0.5, -0.85), "Leg": (0.0, 0.85, -0.5), "Foot": (0.0, 0.4, -1.0)}},
        "sway": {},
    },
    # 回避（下）：前へ突っ込む（体を 40° 余計に倒し、脚は後ろへ伸ばす）
    "RailDodgeDownPose": {
        "base": "RailStand",
        "tilt": 50.0,
        "hips": {"offset": (0.0, 0.0, -0.03), "lean": 4.0},
        "bend": {"Spine": 4.0, "Spine1": 4.0, "Spine2": 2.0, "Neck": -25.0, "Head": -20.0},
        "arms": {"Arm": (-0.7, 0.1, -0.7), "ForeArm": (-0.5, -0.1, -0.85)},
        "legs": GLIDE_LEGS,
        "sway": {},
    },

    # ---- ボス戦（地上）のスライド移動。両足を床に着けたまま。向きは自機から見た向き ----
    # 前：ひざを曲げて腰を落とし、前へ傾ける。腕は少し開いて前腕を前へ
    "BossSlideForward": {
        "base": "BossIdle",
        "hips": {"offset": (0.0, 0.06, -0.2), "lean": 18.0},
        "bend": {"Spine": 9.0, "Spine1": 9.0, "Spine2": 5.0, "Neck": -22.0, "Head": -16.0},
        "arms": {"Arm": (-0.4, 0.05, -1.0), "ForeArm": (-0.2, -0.6, -0.8)},
        "legs": {"ik": True, "knee": (-0.05, -1.0, 0.0), "toe_out": 0.0},
        "sway": {"hips_z": 0.006, "arm": 1.0},
    },
    # 左へ曲がる（スキーのターン）：腰を曲がる側（+X）へ寄せて落とし、両ひざをそちらへ入れ、上体を傾けて少しひねる。
    # 外側（右）の腕は開いてバランスを取り、内側（左）の腕は体の近くへ
    "BossSkiLeft": {
        "base": "BossIdle",
        "hips": {"offset": (0.22, 0.05, -0.26), "lean": 14.0},
        "bend": {"Spine": 3.0, "Spine1": 3.0, "Spine2": 2.0, "Neck": -10.0, "Head": -8.0},
        "rotate": [("Spine", AXIS_SIDE_LEAN, 24.0), ("Spine1", AXIS_TWIST, 14.0)],
        "arms": {"Right": {"Arm": (-0.6, 0.0, -0.8), "ForeArm": (-0.3, -0.6, -0.7)},
                 "Left": {"Arm": (-0.2, -0.1, -1.0), "ForeArm": (0.05, -0.9, -0.4)}},
        "legs": {"ik": True, "knee": (-0.05, -1.0, 0.0), "toe_out": 0.0},
        "sway": {"hips_z": 0.006},
        "call": ski_call(SKI_KNEES_LEFT),
    },
    "BossSkiRight": {
        "base": "BossIdle",
        "hips": {"offset": (-0.22, 0.05, -0.26), "lean": 14.0},
        "bend": {"Spine": 3.0, "Spine1": 3.0, "Spine2": 2.0, "Neck": -10.0, "Head": -8.0},
        "rotate": [("Spine", AXIS_SIDE_LEAN, -24.0), ("Spine1", AXIS_TWIST, -14.0)],
        "arms": {"Right": {"Arm": (-0.2, -0.1, -1.0), "ForeArm": (0.05, -0.9, -0.4)},
                 "Left": {"Arm": (-0.6, 0.0, -0.8), "ForeArm": (-0.3, -0.6, -0.7)}},
        "legs": {"ik": True, "knee": (-0.05, -1.0, 0.0), "toe_out": 0.0},
        "sway": {"hips_z": 0.006},
        "call": ski_call(SKI_KNEES_RIGHT),
    },

    # ---- 地上の回避：空中と同じ一回転。ぐっとしゃがんで逆へひねってから、腰を弧を描くように持ち上げて回る。
    # 待機（BossStandIdle）から始まり待機へ戻る ----
    "GroundRollLeft": {"base": "BossStandIdle", "sway": {},
                       "call": roll_call(1.0, GROUND_ROLL_LIFT, GROUND_ROLL_CROUCH)},
    "GroundRollRight": {"base": "BossStandIdle", "sway": {},
                        "call": roll_call(-1.0, GROUND_ROLL_LIFT, GROUND_ROLL_CROUCH)},
    # 前：飛び込んできりもみ→着地して前転。後ろ：しなるバック宙
    "GroundDiveRoll": {"base": "BossStandIdle", "sway": {}, "call": acro_call(FLIP_FORWARD)},
    "GroundBackFlip": {"base": "BossStandIdle", "sway": {}, "call": acro_call(FLIP_BACK)},
}


def _dodge_keys(start, antic, peak):
    """回避の 1 回きり：元の姿勢 → 予兆（逆の動き）→ いちばん大きい所 → 保つ → 元の姿勢（DODGE_DURATION 秒）"""
    return {"duration": DODGE_DURATION,
            "keys": [(0.0, start), (DODGE_DURATION * DODGE_KEY_ANTIC, antic),
                     (DODGE_DURATION * DODGE_KEY_PEAK, peak), (DODGE_DURATION * DODGE_KEY_HOLD, peak),
                     (DODGE_DURATION, start)]}


CLIPS = {
    "BossIdle": {"duration": 1.0, "pose": "BossStandIdle"},   # ボス戦の待機（ループ・動かない。直立に近い）
    # STG
    "RailStand": {"duration": 2.0, "pose": "RailStand"},   # 立ち姿勢（ループ・小さく揺れる）
    "RailGlide": {"duration": 2.0, "pose": "RailGlide"},   # 滑空（ループ・小さく揺れる）
    "RailDodgeLeft": {"duration": DODGE_DURATION, "pose": "RailRollLeft"},     # 1 回きり（phase で一回転）
    "RailDodgeRight": {"duration": DODGE_DURATION, "pose": "RailRollRight"},
    "RailDodgeUp": _dodge_keys("RailStand", "RailDodgeUpAntic", "RailDodgeUpPose"),
    # 下は予兆なしでまっすぐ突っ込む（2026-11-27 ユーザー）
    "RailDodgeDown": {"duration": DODGE_DURATION,
                      "keys": [(0.0, "RailStand"), (DODGE_DURATION * DODGE_KEY_ANTIC, "RailDodgeDownPose"),
                               (DODGE_DURATION * DODGE_KEY_HOLD, "RailDodgeDownPose"), (DODGE_DURATION, "RailStand")]},
    # ボス戦のスライド移動（ループ）。左右はスキーのターン（自機が回っている間に鳴る）。後ろは作らない（前で代用）
    "BossRunForward": {"duration": SLIDE_LOOP, "pose": "BossSlideForward"},
    "BossRunLeft": {"duration": SLIDE_LOOP, "pose": "BossSkiLeft"},
    "BossRunRight": {"duration": SLIDE_LOOP, "pose": "BossSkiRight"},
    # 地上の回避（1 回きり）：前＝飛び込みきりもみ→前転、後ろ＝バック宙（0.6 秒）。左右は弧を描く一回転（0.4 秒）
    "GroundDodgeForward": {"duration": GROUND_FLIP_DURATION, "pose": "GroundDiveRoll"},
    "GroundDodgeBack": {"duration": GROUND_FLIP_DURATION, "pose": "GroundBackFlip"},
    "GroundDodgeLeft": {"duration": DODGE_DURATION, "pose": "GroundRollLeft"},
    "GroundDodgeRight": {"duration": DODGE_DURATION, "pose": "GroundRollRight"},
}
