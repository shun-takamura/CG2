"""まとまり melee（A 近接）：B1〜4・Y1〜4、溜めの構え（ループ）、溜めを放つ（単独／B4 から／Y4 から）、特別なルートの技。

鳴らすのは MeleeComboPart（Resources/Json/Tuning/MeleeCombo.json の技ごとの clip / holdClip）。
名前の例：MeleeB1〜MeleeB4・MeleeY1〜MeleeY4・MeleeChargeHold・MeleeChargeSolo・MeleeChargeAfterB4・MeleeChargeAfterY4
書き方は 25_PlayerAnimClips.md §3。

武器は右手のライフル＋銃口の下から前へ伸びるビームの刃（銃剣）、左手は盾（MainWeapon.json / Shield.json）。
振りは「胸の向きから見た手首の位置（右肩から）・刃の向き・刃を振る向き」で書き、call（melee_arms）で右腕の IK と
手首のひねりを付ける。胸を基準にするので、上体をひねる・体ごと回すと腕も一緒に回る。
振りが当たる瞬間（刃が正面を通る所）は、技のプレハブ（Resources/Json/Prefabs/PlayerMelee*.json の melee）の
startup に合わせる（24 番 §6.1）。
"""

import numpy as np
from mathutils import Matrix, Quaternion, Vector

MX = "mixamorig:"
Z_AXIS = (0.0, 0.0, 1.0)

# 右手に付くライフルの握りの回転（MainWeapon.json の Standard の grip.rotate の X [ラジアン]）。
# 刃の向き・振る向きから手首のひねりを決めるのに使う（ほかの武器も握りはほぼ同じ）
GRIP_ROTATE_X = -1.53

# 上体のひねり [度] を腰・背骨に分ける割合（腰は脚の IK で足が床に残る）
TWIST_SHARE = {"Hips": 0.4, "Spine": 0.22, "Spine1": 0.2, "Spine2": 0.18}
# 上体をひねった時、首・頭で打ち消して正面（敵）を見る割合
LOOK_SHARE = {"Neck": 0.4, "Head": 0.4}
# 近接の構えの足（足首の位置を BossIdle の足首からずらす [m]。+X＝左・−Y＝前）：左足を前・右足を後ろへ
MELEE_STANCE = {"Right": (-0.03, 0.12), "Left": (0.02, -0.1)}
# 構えのひざを出す向き（右側で書く）
MELEE_KNEE = (-0.25, -1.0, 0.0)
# 腕の IK が届かない時に縮める割合（伸び切ると肘が反ってがたつくので少し手前で止める）
ARM_REACH_LIMIT = 0.995


def _weapon_axes():
    """手のボーンの中でのライフルの前（銃口・刃が伸びる向き）と上（エンジンの武器のローカルの +Z・+Y）。
    glTF の書き出しは関節の向きを Blender のボーンのまま書き、cook が X を反転するので、MX · Rx(握り) を掛けた向き"""
    rot = Matrix.Diagonal((-1.0, 1.0, 1.0)) @ Matrix.Rotation(GRIP_ROTATE_X, 3, "X")
    return rot @ Vector((0.0, 0.0, 1.0)), rot @ Vector((0.0, 1.0, 0.0))


WEAPON_FORWARD, WEAPON_UP = _weapon_axes()


def _frame(forward, up):
    """forward を X、up を Z にする正規直交の行列（列がそれぞれの軸）"""
    x = Vector(forward).normalized()
    z = Vector(up) - x * Vector(up).dot(x)
    z.normalize()
    y = z.cross(x)
    return Matrix((x, y, z)).transposed()


def _posed_yaw(arm, bone):
    """ボーンの今の向きの、基本の姿勢からの水平の回転 [度]（正面 −Y が +X の側へ回ると正）。
    前傾は入れない（腕と刃の向きは水平に回すだけ。前傾まで入れると刃が下を向く）"""
    pb = arm.pose.bones[MX + bone]
    rot = pb.matrix.to_3x3() @ pb.bone.matrix_local.to_3x3().inverted()
    f = rot @ Vector((0.0, -1.0, 0.0))
    return np.degrees(np.arctan2(f.x, -f.y))


def _arm_ik(arm, rig, side, wrist, elbow_hint):
    """腕の 2 本のボーン（Arm・ForeArm）の IK：手首を wrist に置き、ひじを elbow_hint の側へ出す"""
    up_arm = arm.pose.bones[MX + side + "Arm"]
    fore = arm.pose.bones[MX + side + "ForeArm"]
    shoulder = up_arm.head.copy()
    l1, l2 = up_arm.bone.length, fore.bone.length
    axis = wrist - shoulder
    d = min(axis.length, (l1 + l2) * ARM_REACH_LIMIT)
    axis.normalize()
    a = (l1 * l1 - l2 * l2 + d * d) / (2.0 * d)
    h = np.sqrt(max(l1 * l1 - a * a, 0.0))
    perp = elbow_hint - axis * elbow_hint.dot(axis)
    perp.normalize()
    elbow = shoulder + axis * a + perp * h
    rig.pose_bone_toward(arm, up_arm.name, elbow - shoulder)
    rig.pose_bone_toward(arm, fore.name, shoulder + axis * d - elbow)


def _aim_weapon(arm, rig, blade, cut):
    """右手をひねって、刃を blade の向きへ・刃の付いている側（武器の下）を cut（振る向き）へ向ける"""
    hand = arm.pose.bones[MX + "RightHand"]
    rot = _frame(blade, -cut) @ _frame(WEAPON_FORWARD, WEAPON_UP).transposed()
    hand.matrix = Matrix.Translation(hand.head.copy()) @ rot.to_4x4()
    import bpy
    bpy.context.view_layer.update()


def _stance(arm, rig, feet):
    """足首を BossIdle の足首から feet だけずらして置き直す（脚の IK と足の裏を床へ）"""
    for side, (dx, dy) in feet.items():
        ankle = rig.body_ankle_target(arm, side) + Vector((dx, dy, 0.0))
        rig.leg_ik(arm, side, ankle, MELEE_KNEE)
        rig.foot_flat(arm, side, 0.0)


def melee_arms(right=None, left=None, look=True, feet=None):
    """姿勢の call に入れる関数を作る。
    向きはどれも「胸の水平の向き」から見る（上体のひねり・体ごとの回転で一緒に回り、前傾では傾かない）。
    right：{"wrist": 右肩から手首（胸の向きから見て、+X＝左・−Y＝前・+Z＝上 [m]）, "elbow": ひじを出す向き,
            "blade": 刃の向き, "cut": 刃を振る向き}（向きは胸から見て）
    left：{"wrist": 左肩から手首, "elbow": ひじを出す向き}（盾の腕。省くと元の姿勢のまま）
    look：上体のひねりを首・頭で打ち消して正面を見る
    feet：{side: (dx, dy)} 足首をずらして置き直す（省くと元の姿勢の足のまま）"""
    def call(arm, rig, phase):
        if feet:
            _stance(arm, rig, feet)
        yaw = _posed_yaw(arm, "Spine2")
        chest = Quaternion(Vector(Z_AXIS), np.radians(yaw))
        if look:
            # 胸の正面が水平に回った分を首と頭で戻す
            for bone, share in LOOK_SHARE.items():
                rig.rotate_pose_bone(arm, MX + bone, Quaternion(Vector(Z_AXIS), np.radians(-yaw * share)))
        if left:
            shoulder = arm.pose.bones[MX + "LeftArm"].head.copy()
            _arm_ik(arm, rig, "Left", shoulder + chest @ Vector(left["wrist"]), chest @ Vector(left["elbow"]))
        if right:
            shoulder = arm.pose.bones[MX + "RightArm"].head.copy()
            _arm_ik(arm, rig, "Right", shoulder + chest @ Vector(right["wrist"]), chest @ Vector(right["elbow"]))
            _aim_weapon(arm, rig, chest @ Vector(right["blade"]), chest @ Vector(right["cut"]))
    return call


def melee_pose(twist=0.0, spin=0.0, hips=None, bend=None, right=None, left=None, look=True, legs=None,
               feet=MELEE_STANCE):
    """近接の姿勢を作る（元は BossIdle）。twist：上体を左へひねる [度]（負で右）、腰・背骨に分ける。
    spin：最後に体ごと Z のまわりに回す [度]（正で左回り＝右手が前へ出る向き。回転斬り用。足も一緒に回すので
    構えの足は置き直さない）"""
    pose = {"base": "BossIdle",
            "rotate": [(bone, Z_AXIS, twist * share) for bone, share in TWIST_SHARE.items()],
            "call": melee_arms(right, left, look, None if spin else feet)}
    if hips:
        pose["hips"] = hips
    if bend:
        pose["bend"] = bend
    if spin:
        pose["rotate_after"] = [("Hips", Z_AXIS, spin)]
    if legs:
        pose["legs"] = legs
    return pose


# 突きは刃の向きへ振るので、刃の付いている側（武器の下）は下へ向ける（cut に刃と平行な向きを書くとひねりが決まらない）
# 盾の腕：胸の前で構える
SHIELD_GUARD = {"wrist": (-0.12, -0.3, -0.12), "elbow": (0.6, 0.0, -1.0)}
# 盾の腕：振りの反動で体の横へ引く
SHIELD_BACK = {"wrist": (0.12, 0.05, -0.3), "elbow": (0.6, 0.3, -0.5)}
# 盾の腕：振りかぶりの釣り合いで前へ出す
SHIELD_FORWARD = {"wrist": (-0.05, -0.34, 0.0), "elbow": (0.6, 0.0, -1.0)}

# 足の置き方（MELEE_STANCE と同じ書き方）。2026-11-27 ユーザー：腰と下半身を使い、全身をオーバーに
FEET_BACK = {"Right": (-0.04, 0.2), "Left": (0.02, -0.08)}     # 体重を後ろ足へ（振りかぶり）
FEET_LUNGE = {"Right": (-0.04, 0.24), "Left": (0.03, -0.24)}   # 左足を大きく前へ踏み込む
FEET_RIGHT_STEP = {"Right": (-0.05, -0.3), "Left": (0.03, 0.12)}   # 右足から踏み込む（突き）

POSES = {
    # ---- B1：袈裟斬り（startup 0.08 / active 0.18 / recovery 0） ----
    # 頭の上まで振りかぶり（体重は後ろ足・上体を右へひねって反る）、肘を伸ばしながら左下まで振り下ろす
    "MeleeB1Wind": melee_pose(
        twist=-45.0, hips={"offset": (0.03, 0.08, -0.1), "lean": -4.0}, bend={"Spine": -4.0, "Spine1": -4.0},
        right={"wrist": (0.1, 0.1, 0.32), "elbow": (-1.0, 0.2, 0.3),
               "blade": (-0.15, 0.75, 0.6), "cut": (0.0, -0.6, 0.8)},
        left=SHIELD_FORWARD, feet=FEET_BACK),
    "MeleeB1Hit": melee_pose(
        twist=5.0, hips={"offset": (0.0, -0.06, -0.16), "lean": 14.0}, bend={"Spine": 8.0, "Spine1": 6.0},
        right={"wrist": (0.12, -0.36, 0.1), "elbow": (-1.0, 0.0, -0.3),
               "blade": (0.35, -0.75, -0.55), "cut": (0.3, 0.3, -0.9)},
        left=SHIELD_BACK, feet=FEET_LUNGE),
    "MeleeB1Follow": melee_pose(
        twist=55.0, hips={"offset": (-0.02, -0.1, -0.22), "lean": 22.0}, bend={"Spine": 10.0, "Spine1": 8.0},
        right={"wrist": (0.3, -0.14, -0.22), "elbow": (-1.0, -0.3, 0.0),
               "blade": (0.55, 0.5, -0.6), "cut": (0.3, 0.8, 0.2)},
        left=SHIELD_BACK, feet=FEET_LUNGE),

    # ---- B2：返しの横薙ぎ（左から右へ。startup 0.08 / active 0.18 / recovery 0） ----
    # B1 の振り抜きから刃を左へ返し、肘を伸ばしながら右の少し後ろまで振り切る（腰から回す）
    "MeleeB2Wind": melee_pose(
        twist=55.0, hips={"offset": (-0.02, -0.06, -0.2), "lean": 14.0}, bend={"Spine": 6.0},
        right={"wrist": (0.3, -0.06, -0.12), "elbow": (0.0, 0.3, -1.0),
               "blade": (0.85, 0.45, -0.1), "cut": (0.0, -1.0, 0.0)},
        left=SHIELD_BACK, feet=FEET_LUNGE),
    "MeleeB2Hit": melee_pose(
        twist=0.0, hips={"offset": (0.0, -0.06, -0.2), "lean": 12.0}, bend={"Spine": 4.0},
        right={"wrist": (0.04, -0.4, 0.0), "elbow": (-1.0, 0.0, -0.5),
               "blade": (-0.1, -1.0, 0.0), "cut": (-1.0, 0.0, 0.0)},
        left=SHIELD_GUARD, feet=FEET_LUNGE),
    "MeleeB2Follow": melee_pose(
        twist=-60.0, hips={"offset": (0.02, -0.02, -0.2), "lean": 10.0}, bend={"Spine": 4.0},
        right={"wrist": (-0.37, 0.12, 0.02), "elbow": (0.0, 0.3, -1.0),
               "blade": (-0.7, 0.7, 0.0), "cut": (0.7, 0.7, 0.0)},
        left=SHIELD_FORWARD, feet=FEET_LUNGE),

    # ---- B3：突き（startup 0.10 / active 0.20 / recovery 0.05） ----
    # 右腰へ引いて体重を後ろへ → 右足から踏み込み、右肩を前へ入れて腕を伸ばし切る
    "MeleeB3Wind": melee_pose(
        twist=-40.0, hips={"offset": (0.0, 0.08, -0.14), "lean": 2.0},
        right={"wrist": (0.0, 0.15, -0.1), "elbow": (-0.3, 1.0, -0.3),
               "blade": (0.1, -1.0, 0.05), "cut": (0.0, 0.0, -1.0)},
        left=SHIELD_FORWARD, feet=FEET_BACK),
    "MeleeB3Hit": melee_pose(
        twist=45.0, hips={"offset": (0.0, -0.16, -0.2), "lean": 18.0}, bend={"Spine": 6.0},
        right={"wrist": (0.1, -0.4, 0.04), "elbow": (-1.0, 0.0, -0.4),
               "blade": (0.0, -1.0, -0.05), "cut": (0.0, 0.0, -1.0)},
        left=SHIELD_BACK, feet=FEET_RIGHT_STEP),

    # ---- B4：回転斬り（締め。startup 0.15 / active 0.25 / recovery 0.25） ----
    # 刃を体の左へ引いて左へひねり、体ごと右回りに 1 回転（刃が正面を通るのが 0.15 秒）、
    # 最後は返し（B2）のように肘を伸ばして右の後ろまで振り切る
    "MeleeB4Wind": melee_pose(
        twist=50.0, spin=25.0, hips={"offset": (0.0, 0.0, -0.2), "lean": 8.0},
        right={"wrist": (0.3, -0.12, -0.08), "elbow": (0.0, 0.3, -1.0),
               "blade": (0.9, 0.35, -0.05), "cut": (0.0, -1.0, 0.0)},
        left=SHIELD_BACK),
}
# 回転の途中（体ごと回す角度だけ違う。刃は体の左へ水平に出し、刃が進む向きへ振る）
for _deg in (90, 210, 330):
    POSES[f"MeleeB4Spin{_deg}"] = melee_pose(
        twist=20.0, spin=-float(_deg), hips={"offset": (0.0, 0.0, -0.18), "lean": 6.0}, look=False,
        right={"wrist": (0.3, -0.2, 0.0), "elbow": (0.0, 0.3, -1.0),
               "blade": (1.0, -0.1, 0.0), "cut": (0.0, -1.0, 0.0)},
        left=SHIELD_BACK)
POSES["MeleeB4Follow"] = melee_pose(
    twist=-65.0, hips={"offset": (0.02, -0.04, -0.22), "lean": 12.0}, bend={"Spine": 4.0},
    right={"wrist": (-0.37, 0.12, 0.0), "elbow": (0.0, 0.3, -1.0),
           "blade": (-0.65, 0.75, -0.05), "cut": (0.75, 0.65, 0.0)},
    left=SHIELD_FORWARD, feet=FEET_LUNGE)

CLIPS = {
    # 当たる瞬間（Hit）を startup に置く。振り抜いて止め、スキ（lock）が明けたら待機へ戻る
    "MeleeB1": {"duration": 0.65, "keys": [(0.0, "BossIdle"), (0.05, "MeleeB1Wind"), (0.08, "MeleeB1Hit", "in"),
                                          (0.18, "MeleeB1Follow"), (0.32, "MeleeB1Follow"), (0.65, "BossIdle")]},
    "MeleeB2": {"duration": 0.65, "keys": [(0.0, "BossIdle"), (0.05, "MeleeB2Wind"), (0.08, "MeleeB2Hit", "in"),
                                          (0.2, "MeleeB2Follow"), (0.32, "MeleeB2Follow"), (0.65, "BossIdle")]},
    "MeleeB3": {"duration": 0.7, "keys": [(0.0, "BossIdle"), (0.06, "MeleeB3Wind"), (0.1, "MeleeB3Hit", "in"),
                                         (0.34, "MeleeB3Hit"), (0.7, "BossIdle")]},
    "MeleeB4": {"duration": 1.05, "keys": [(0.0, "BossIdle"), (0.08, "MeleeB4Wind"),
                                          (0.15, "MeleeB4Spin90", "in"), (0.23, "MeleeB4Spin210", "linear"),
                                          (0.31, "MeleeB4Spin330", "linear"), (0.44, "MeleeB4Follow"),
                                          (0.68, "MeleeB4Follow"), (1.05, "BossIdle")]},
}
