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


# T ポーズの手のひらの向き（アーマチュアの空間。rig_player_mech.py の HAND_REST_PALM_NORMAL と同じく下向き）
REST_PALM = (0.0, 0.0, -1.0)


def _turn_palm(arm, rig, side, palm):
    """前腕を自分の軸のまわりにひねって、手のひらを palm の向きへ（手は前腕に付いて回る）"""
    fore = arm.pose.bones[MX + side + "ForeArm"]
    hand = arm.pose.bones[MX + side + "Hand"]
    axis = (fore.tail - fore.head).normalized()
    rest = hand.bone.matrix_local.to_3x3()
    now = (hand.matrix.to_3x3() @ rest.inverted()) @ Vector(REST_PALM)
    cur = now - axis * now.dot(axis)
    want = Vector(palm) - axis * Vector(palm).dot(axis)
    if cur.length < 1e-6 or want.length < 1e-6:
        return
    rig.rotate_pose_bone(arm, fore.name, cur.normalized().rotation_difference(want.normalized()))


def _stance(arm, rig, feet):
    """足首を BossIdle の足首から feet だけずらして置き直す（脚の IK と足の裏を床へ）"""
    for side, (dx, dy) in feet.items():
        ankle = rig.body_ankle_target(arm, side) + Vector((dx, dy, 0.0))
        rig.leg_ik(arm, side, ankle, MELEE_KNEE)
        rig.foot_flat(arm, side, 0.0)


def _pivot_spin(arm, deg, side):
    """体ごと Z のまわりに deg 度回す。中心は side の足首（軸足は床に残り、反対の足が回る）"""
    hips = arm.pose.bones[MX + "Hips"]
    foot = arm.pose.bones[MX + side + "Foot"].head
    pivot = Vector((foot.x, foot.y, 0.0))
    rot = Matrix.Rotation(np.radians(deg), 4, "Z")
    hips.matrix = Matrix.Translation(pivot) @ rot @ Matrix.Translation(-pivot) @ hips.matrix
    import bpy
    bpy.context.view_layer.update()


def melee_arms(right=None, left=None, look=True, feet=None, pivot_spin=None):
    """姿勢の call に入れる関数を作る。
    向きはどれも「胸の水平の向き」から見る（上体のひねり・体ごとの回転で一緒に回り、前傾では傾かない）。
    right：{"wrist": 右肩から手首（胸の向きから見て、+X＝左・−Y＝前・+Z＝上 [m]）, "elbow": ひじを出す向き,
            "blade": 刃の向き, "cut": 刃を振る向き}（向きは胸から見て）
    left：{"wrist": 左肩から手首, "elbow": ひじを出す向き}（盾の腕。省くと元の姿勢のまま）。
          {"grip": 右手首から銃身に沿った距離 [m], "elbow": …} なら左手でライフルを握る（両手持ち）。
          "palm" があれば前腕をひねって左手の手のひらをその向きへ。
          右手の手のひらは武器の上（＝cut の逆）を向く
    look：上体のひねりを首・頭で打ち消して正面を見る
    feet：{side: (dx, dy)} 足首をずらして置き直す（省くと元の姿勢の足のまま）
    pivot_spin：(度, 軸足の side) 足を置いた後に、軸足の足首を中心に体ごと回す（正で左回り）"""
    def call(arm, rig, phase):
        if feet:
            _stance(arm, rig, feet)
        if pivot_spin:
            _pivot_spin(arm, pivot_spin[0], pivot_spin[1])
        yaw = _posed_yaw(arm, "Spine2")
        chest = Quaternion(Vector(Z_AXIS), np.radians(yaw))
        if look:
            # 胸の正面が水平に回った分を首と頭で戻す
            for bone, share in LOOK_SHARE.items():
                rig.rotate_pose_bone(arm, MX + bone, Quaternion(Vector(Z_AXIS), np.radians(-yaw * share)))
        if right:
            shoulder = arm.pose.bones[MX + "RightArm"].head.copy()
            _arm_ik(arm, rig, "Right", shoulder + chest @ Vector(right["wrist"]), chest @ Vector(right["elbow"]))
            _aim_weapon(arm, rig, chest @ Vector(right["blade"]), chest @ Vector(right["cut"]))
        if left and "grip" in left:
            # 両手持ち：右手首から刃の向きへ grip だけ進んだ所に左手首を置く（右腕を先に決めてから）
            hand = arm.pose.bones[MX + "RightHand"].head.copy()
            target = hand + (chest @ Vector(right["blade"])).normalized() * left["grip"]
            _arm_ik(arm, rig, "Left", target, chest @ Vector(left["elbow"]))
        elif left:
            shoulder = arm.pose.bones[MX + "LeftArm"].head.copy()
            _arm_ik(arm, rig, "Left", shoulder + chest @ Vector(left["wrist"]), chest @ Vector(left["elbow"]))
        if left and "palm" in left:
            _turn_palm(arm, rig, "Left", chest @ Vector(left["palm"]))
    return call


def melee_pose(twist=0.0, spin=0.0, hips=None, bend=None, right=None, left=None, look=True, legs=None,
               feet=MELEE_STANCE, rotate=None, pivot_spin=None):
    """近接の姿勢を作る（元は BossIdle）。twist：上体を左へひねる [度]（負で右）、腰・背骨に分ける。
    spin：最後に体ごと Z のまわりに回す [度]（正で左回り＝右手が前へ出る向き。回転斬り用。足も一緒に回すので
    構えの足は置き直さない）。rotate：ひねりの後に足す回転（[(ボーン, 軸, 度), …]、鎖骨を引くなど）。
    pivot_spin：(度, 軸足の side) 軸足を中心に体ごと回す（spin と違って足を置いた後に回すので、軸足は床に残る）"""
    pose = {"base": "BossIdle",
            "rotate": [(bone, Z_AXIS, twist * share) for bone, share in TWIST_SHARE.items()] + (rotate or []),
            "call": melee_arms(right, left, look, None if spin else feet, pivot_spin)}
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

# ---- 溜め攻撃（24 番 §2。Y+B を押している間は holdClip をループ、離すか最大（1.2 秒）で放つ） ----
# 2026-11-27 ユーザーの参考画像：Y4 から＝浮き上がりながら頭上へ持ち上げて両手で振り下ろす／
# B4 から＝少し浮いて前へ突き出して溜めて撃ち、右手は反動で大きく後ろへ
# 左の鎖骨を後ろへ引く角度 [度]（Z まわり、正で鎖骨の先が後ろへ）
LEFT_SHOULDER_BACK = 20.0
# 溜めている間に体を浮かせる高さ [m]（ゲームの自機の位置は床のまま。腰を上げ、脚は床から離れて垂らす）
FLOAT_HIGH = 0.45
FLOAT_LOW = 0.15
# 浮いている間の脚（右側で書く）：右ひざを軽く曲げ、左は伸ばし気味に垂らす
LEGS_FLOAT = {"ik": False,
              "Right": {"UpLeg": (0.0, -0.35, -1.0), "Leg": (0.0, 0.45, -1.0), "Foot": (0.0, -0.4, -1.0)},
              "Left": {"UpLeg": (0.0, 0.05, -1.0), "Leg": (0.0, 0.25, -1.0), "Foot": (0.0, -0.3, -1.0)}}
# 手のひらを上へ（右手は武器の上＝cut の逆が手のひらの向きなので、cut を下へ）
PALM_UP = (0.0, 0.0, 1.0)
PALM_UP_CUT = (0.0, 0.0, -1.0)
# 浮いたまま足を軽く前後に開く（左足が前・右足が後ろ。右側で書いた向き）。
# Y4 からの溜めは、溜め始めから振り下ろし終わりまでずっとこの脚（2026-11-27 ユーザー）
LEGS_SPLIT_FLOAT = {"ik": False,
                    "Right": {"UpLeg": (0.0, 0.25, -1.0), "Leg": (0.0, 0.55, -1.0), "Foot": (0.0, 0.0, -1.0)},
                    "Left": {"UpLeg": (0.0, -0.3, -1.0), "Leg": (0.0, 0.0, -1.0), "Foot": (0.0, -0.7, -1.0)}}


def legs_swing(base, hip, knee=0.0):
    """浮いている脚 base を上体の振りに合わせて振る（脚の向きは絶対の向きで書くので、腰を傾けても付いてこない）。
    hip：太ももを X 軸まわりに回す [度]（正で後ろへ流れる・負で前へ引き上げる）、knee：すね・足をさらに後ろへ曲げる [度]"""
    def turn(v, deg):
        a = np.radians(deg)
        x, y, z = v
        return (x, y * np.cos(a) - z * np.sin(a), y * np.sin(a) + z * np.cos(a))
    out = {"ik": False}
    for side in ("Right", "Left"):
        out[side] = {"UpLeg": turn(base[side]["UpLeg"], hip),
                     "Leg": turn(base[side]["Leg"], hip + knee),
                     "Foot": turn(base[side]["Foot"], hip + knee)}
    return out


# Y4 からの溜めで、上体の振りに合わせた脚（2026-11-27 ユーザー：上体から振り下ろしているのに下半身が動かない）
# 振りかぶり＝脚を後ろへ流して弓なり／当たる瞬間＝前へ出てくる／振り切り＝太ももを前へ引き上げてひざを曲げ「く」の字
LEGS_Y4_WIND = legs_swing(LEGS_SPLIT_FLOAT, 25.0, 25.0)
LEGS_Y4_HIT = legs_swing(LEGS_SPLIT_FLOAT, -15.0, 15.0)
LEGS_Y4_FOLLOW = legs_swing(LEGS_SPLIT_FLOAT, -45.0, 50.0)
# 両手持ちの左手（右手首から銃身に沿って）
TWO_HAND = {"grip": 0.22, "elbow": (1.0, 0.0, -0.6)}

# Y4 から：溜めながらゆっくり浮き上がり、武器を頭上へ持ち上げて両手で握る → 一気に振り下ろして着地
POSES.update({
    # 右腕を体の右下へ下ろし、刃も右下へ（持ち上げ始め）
    "MeleeChargeY4Low": melee_pose(
        twist=0.0, hips={"offset": (0.0, 0.0, FLOAT_HIGH * 0.15), "lean": 2.0}, legs=LEGS_SPLIT_FLOAT, feet=None,
        right={"wrist": (-0.26, -0.02, -0.3), "elbow": (-0.3, 1.0, 0.0),
               "blade": (-0.6, -0.1, -0.8), "cut": PALM_UP_CUT},
        left={"wrist": (0.2, 0.0, -0.32), "elbow": (0.3, 1.0, 0.0), "palm": PALM_UP}),
    # 右腕を真横に水平まで上げ、刃は外へ。左腕も横へ開き始める
    "MeleeChargeY4Lift": melee_pose(
        twist=0.0, hips={"offset": (0.0, 0.02, FLOAT_HIGH * 0.55), "lean": 0.0}, legs=LEGS_SPLIT_FLOAT, feet=None,
        right={"wrist": (-0.4, 0.0, 0.03), "elbow": (0.0, 1.0, 0.0),
               "blade": (-1.0, 0.0, 0.15), "cut": PALM_UP_CUT},
        left={"wrist": (0.32, 0.0, -0.18), "elbow": (0.0, 1.0, 0.0), "palm": PALM_UP}),
    "MeleeChargeY4Top": melee_pose(
        twist=0.0, hips={"offset": (0.0, 0.04, FLOAT_HIGH), "lean": -8.0}, bend={"Spine": -4.0, "Spine1": -4.0},
        legs=LEGS_SPLIT_FLOAT, feet=None,
        right={"wrist": (0.18, 0.02, 0.36), "elbow": (-1.0, 0.0, 0.2),
               "blade": (0.0, 0.15, 1.0), "cut": (0.0, -1.0, 0.0)},
        left=TWO_HAND),
    # 放つ 1：後ろへ大きく反って振りかぶる。武器は頭の後ろで下を向き、足は前後に開く（2026-11-27 ユーザーのスケッチ）
    "MeleeChargeY4Wind": melee_pose(
        twist=0.0, hips={"offset": (0.0, 0.1, FLOAT_HIGH), "lean": -24.0},
        bend={"Spine": -10.0, "Spine1": -10.0, "Spine2": -6.0, "Neck": 12.0, "Head": 8.0},
        legs=LEGS_Y4_WIND, feet=None,
        right={"wrist": (0.16, 0.18, 0.3), "elbow": (-1.0, 0.3, 0.3),
               "blade": (0.0, 0.6, -0.8), "cut": (0.0, 0.8, 0.6)},
        left=TWO_HAND),
    # 放つ 2：振り下ろしの途中（当たる瞬間）。刃が正面を通り、上体は前へ折れていく。まだ浮いている
    "MeleeChargeY4Hit": melee_pose(
        twist=0.0, hips={"offset": (0.0, -0.04, FLOAT_HIGH * 0.8), "lean": 18.0},
        bend={"Spine": 10.0, "Spine1": 8.0, "Neck": -14.0, "Head": -10.0}, legs=LEGS_Y4_HIT, feet=None,
        right={"wrist": (0.18, -0.38, 0.05), "elbow": (-1.0, 0.0, -0.3),
               "blade": (0.0, -0.85, -0.5), "cut": (0.0, 0.5, -0.85)},
        left=TWO_HAND),
    # 放つ 3：振り切った所。浮いたまま上体を前へ丸め、両腕を前に下ろして武器は脚の前で真下へ。顔は正面
    "MeleeChargeY4Follow": melee_pose(
        twist=0.0, hips={"offset": (0.0, -0.06, FLOAT_HIGH * 0.6), "lean": 30.0},
        bend={"Spine": 14.0, "Spine1": 12.0, "Spine2": 6.0, "Neck": -30.0, "Head": -20.0}, legs=LEGS_Y4_FOLLOW, feet=None,
        right={"wrist": (0.2, -0.24, -0.3), "elbow": (-1.0, 0.0, 0.0),
               "blade": (0.0, -0.15, -1.0), "cut": (0.0, 1.0, -0.15)},
        left=TWO_HAND),
    # 着地：深く沈み、足は前後に開いて踏ん張る。武器は前の地面へ
    "MeleeChargeY4Land": melee_pose(
        twist=0.0, hips={"offset": (0.0, -0.1, -0.3), "lean": 26.0},
        bend={"Spine": 12.0, "Spine1": 8.0, "Neck": -16.0, "Head": -10.0},
        right={"wrist": (0.2, -0.34, -0.22), "elbow": (-1.0, 0.0, -0.2),
               "blade": (0.0, -0.4, -0.9), "cut": (0.0, 0.9, -0.4)},
        left=TWO_HAND, feet=FEET_LUNGE),
})

# B4 から：少し浮いて武器を前へ突き出して溜める → 撃つ。右手は反動で大きく後ろへ、体も後ろへ押される
POSES.update({
    "MeleeChargeB4Aim": melee_pose(
        twist=40.0, hips={"offset": (0.0, 0.0, FLOAT_LOW), "lean": 6.0}, legs=LEGS_FLOAT, feet=None,
        rotate=[("LeftShoulder", Z_AXIS, LEFT_SHOULDER_BACK)],
        right={"wrist": (0.1, -0.4, 0.06), "elbow": (-1.0, 0.0, -0.3),
               "blade": (0.0, -1.0, 0.0), "cut": (0.0, 0.0, -1.0)},
        left={"wrist": (0.1, 0.3, -0.18), "elbow": (1.0, 0.2, -0.3)}),
    # 撃った反動 1：肘を曲げながら右腕が右横へ水平に流れる。上体は右へ開き、体は後ろへ押される
    "MeleeChargeB4RecoilSide": melee_pose(
        twist=-25.0, hips={"offset": (0.0, 0.08, FLOAT_LOW + 0.03), "lean": -6.0}, legs=LEGS_FLOAT, feet=None,
        right={"wrist": (-0.3, -0.12, 0.03), "elbow": (-0.4, 1.0, 0.0),
               "blade": (-0.5, -0.85, 0.05), "cut": (-0.85, 0.5, 0.0)},
        left={"wrist": (0.05, -0.05, -0.2), "elbow": (1.0, 0.3, -0.6)}),
    # 撃った反動 2：右腕は後ろまで回って肘が軽く伸び、左手は前へ。体はさらに後ろへ押されて反る
    "MeleeChargeB4Recoil": melee_pose(
        twist=-50.0, hips={"offset": (0.0, 0.14, FLOAT_LOW + 0.05), "lean": -12.0},
        bend={"Spine": -6.0, "Spine1": -6.0, "Neck": 6.0}, legs=LEGS_FLOAT, feet=None,
        right={"wrist": (-0.2, 0.33, 0.04), "elbow": (-1.0, 0.3, 0.0),
               "blade": (-0.25, 0.95, 0.1), "cut": (0.95, 0.25, 0.0)},
        left={"wrist": (-0.08, -0.36, 0.0), "elbow": (1.0, 0.0, -0.6)}),
    # 着地：後ろへ押されたまま踏ん張る
    "MeleeChargeB4Land": melee_pose(
        twist=-20.0, hips={"offset": (0.0, 0.06, -0.2), "lean": 10.0},
        right={"wrist": (-0.1, 0.0, -0.05), "elbow": (-1.0, 0.3, -0.3),
               "blade": (-0.2, -0.9, 0.1), "cut": (0.0, 0.0, -1.0)},
        left=SHIELD_GUARD, feet=FEET_BACK),
})

CLIPS.update({
    # 溜めている間（ループ）。段階 3 に着くのが 1.2 秒（MeleeCombo.json の holdTime）なので、1.2 秒で頭上に着き、
    # その後は動かない（最大で自動で放つので、ループの継ぎ目までは行かない）
    "MeleeChargeHoldAfterY4": {"duration": 1.5, "keys": [(0.0, "BossIdle"), (0.25, "MeleeChargeY4Low"),
                                                        (0.7, "MeleeChargeY4Lift"), (1.2, "MeleeChargeY4Top"),
                                                        (1.5, "MeleeChargeY4Top")]},
    # 放つ（startup 0.25 / active 0.35 / recovery 0.5）：頭上 → 後ろへ大きく振りかぶる → 0.25 秒で正面を通る →
    # 浮いたまま振り切る → 着地
    "MeleeChargeAfterY4": {"duration": 1.4, "keys": [(0.0, "MeleeChargeY4Top"), (0.15, "MeleeChargeY4Wind"),
                                                    (0.25, "MeleeChargeY4Hit", "in"),
                                                    (0.33, "MeleeChargeY4Follow"),
                                                    (0.6, "MeleeChargeY4Follow"),
                                                    (0.78, "MeleeChargeY4Land", "in"),
                                                    (1.0, "MeleeChargeY4Land"), (1.4, "BossIdle")]},
    # 溜めている間（ループ）：0.3 秒で浮いて構え、あとは動かない（継ぎ目までは行かない）
    "MeleeChargeHoldAfterB4": {"duration": 1.5, "keys": [(0.0, "BossIdle"), (0.3, "MeleeChargeB4Aim"),
                                                        (1.5, "MeleeChargeB4Aim")]},
    # 放つ（startup 0.1 / active 0.35 / recovery 0.5）：構えたまま 0.1 秒で撃ち、反動で右腕が右から後ろへ回る → 着地
    "MeleeChargeAfterB4": {"duration": 1.2, "keys": [(0.0, "MeleeChargeB4Aim"), (0.1, "MeleeChargeB4Aim"),
                                                    (0.16, "MeleeChargeB4RecoilSide", "linear"),
                                                    (0.26, "MeleeChargeB4Recoil"),
                                                    (0.45, "MeleeChargeB4Recoil"),
                                                    (0.7, "MeleeChargeB4Land", "in"), (1.2, "BossIdle")]},
})

# ---- Y（強）コンボ（2026-11-28 ユーザーのスケッチ。24 番 §8：2 回目以降の判定・攻撃中の移動・前方ガード） ----
# 判定の時間は MeleeCombo.json の Y1〜Y4（hits / motions / guard）とプレハブ PlayerMeleeY*.json の startup に合わせる

# Y の技は上体をもう少し前へ倒す（2026-11-28 ユーザー）。hips の lean に足す [度]
Y_LEAN_ADD = 6.0
# 盾の腕：正面に突き出して構える（Y1 の突進）
SHIELD_FRONT = {"wrist": (-0.06, -0.36, 0.06), "elbow": (1.0, 0.0, -0.5)}
# 盾の腕：肩ごと後ろへ引く（Y3 の①）
SHIELD_PULL = {"wrist": (0.1, 0.3, -0.16), "elbow": (1.0, 0.2, -0.3)}
# 左の鎖骨を後ろへ引く（Z まわり・正で鎖骨の先が後ろへ）
PULL_LEFT_SHOULDER = [("LeftShoulder", Z_AXIS, 20.0)]
# 跳んでいる間の脚（右側で書く）：両ひざを曲げて畳む
LEGS_JUMP_TUCK = {"ik": False,
                  "Right": {"UpLeg": (0.0, -0.6, -1.0), "Leg": (0.0, 0.6, -1.0), "Foot": (0.0, -0.2, -1.0)},
                  "Left": {"UpLeg": (0.0, -0.2, -1.0), "Leg": (0.0, 0.9, -1.0), "Foot": (0.0, 0.3, -1.0)}}
# Y3 の跳ぶ高さ [m]（腰を上げるだけ。前へ進むのはゲームの移動）
Y3_JUMP_HIGH = 0.55
# 回転斬りの足：進む方向へ左足を踏み出して軸足にする → 回り終わりで右足が前
FEET_PIVOT_LEFT = {"Right": (-0.04, 0.16), "Left": (0.03, -0.32)}
FEET_RIGHT_FRONT = {"Right": (-0.05, -0.26), "Left": (0.03, 0.14)}
# Y4 の②：左足も引いて下がる／③：右足を左足の少し後ろに合わせる
FEET_Y4_BACK = {"Right": (-0.05, -0.08), "Left": (0.03, 0.22)}
FEET_Y4_CLOSE = {"Right": (-0.05, 0.48), "Left": (0.03, 0.3)}
# 回転のキーの間隔 [度]（球面線形のつなぎが近い向きを通るよう 180 未満。細かいほど軸足のまわりの円に近い）
SPIN_STEP = 60


def y_pose(**kw):
    """Y の技の姿勢（melee_pose に、上体の前傾 Y_LEAN_ADD を足す）"""
    hips = dict(kw.pop("hips", {"offset": (0.0, 0.0, 0.0), "lean": 0.0}))
    hips["lean"] = hips.get("lean", 0.0) + Y_LEAN_ADD
    return melee_pose(hips=hips, **kw)


def spin_coil_pose():
    """回転斬りの構え：刃を体の左へ引いて左へひねる。足はまだ踏み込まず、体重は後ろ（この後に左足を踏み込んでから回る）"""
    return y_pose(twist=45.0, hips={"offset": (0.0, 0.06, -0.16), "lean": 2.0},
                  right={"wrist": (0.3, -0.1, -0.08), "elbow": (0.0, 0.3, -1.0),
                         "blade": (0.9, 0.4, -0.1), "cut": (0.0, -1.0, 0.0)},
                  left=SHIELD_BACK, feet=FEET_BACK)


# 右回りの回転斬りの途中：刃は体の左へ水平、左足を軸に体ごと回る
SPIN_RIGHT_ARM = {"wrist": (0.3, -0.2, -0.04), "elbow": (0.0, 0.3, -1.0),
                  "blade": (1.0, -0.1, -0.05), "cut": (0.0, -1.0, 0.0)}


def spin_pose(deg, height, lean):
    return y_pose(twist=20.0, hips={"offset": (0.0, 0.0, height), "lean": lean}, look=False,
                  bend={"Spine": 6.0, "Spine1": 4.0}, right=SPIN_RIGHT_ARM, left=SHIELD_BACK,
                  feet=FEET_PIVOT_LEFT, pivot_spin=(-float(deg), "Left"))


POSES.update({
    # ---- Y1：盾を構えて突進し、突く（突進 0.05〜0.4 秒・ガード 0〜0.45 秒・突きの判定 0.45 秒） ----
    # 盾を正面へ構えて低く突っ込む。武器は右腰へ引いて突きの用意
    "MeleeY1Charge": y_pose(
        twist=-25.0, hips={"offset": (0.0, -0.06, -0.2), "lean": 22.0}, bend={"Spine": 8.0, "Spine1": 6.0},
        right={"wrist": (-0.02, 0.16, -0.14), "elbow": (-0.3, 1.0, -0.3),
               "blade": (0.05, -1.0, 0.05), "cut": (0.0, 0.0, -1.0)},
        left=SHIELD_FRONT, feet=FEET_LUNGE),
    # 突き：盾を引き、右肩を入れて腕を伸ばし切る
    "MeleeY1Hit": y_pose(
        twist=50.0, hips={"offset": (0.0, -0.16, -0.22), "lean": 20.0}, bend={"Spine": 6.0},
        right={"wrist": (0.1, -0.41, 0.04), "elbow": (-1.0, 0.0, -0.4),
               "blade": (0.0, -1.0, -0.03), "cut": (0.0, 0.0, -1.0)},
        left=SHIELD_BACK, feet=FEET_RIGHT_STEP),

    # ---- Y2：止まらずに 2 回転する右回りの回転斬り（判定 0.4 秒・0.9 秒、0〜0.7 秒は向きを決められる） ----
    # 構え → 進む方向へ左足を踏み込む（この間にスティックで向きを決める）。踏み込んでから回る
    "MeleeY2Coil": spin_coil_pose(),
    "MeleeY2Wind": y_pose(
        twist=50.0, hips={"offset": (0.0, -0.06, -0.22), "lean": 12.0}, bend={"Spine": 6.0},
        right={"wrist": (0.3, -0.12, -0.1), "elbow": (0.0, 0.3, -1.0),
               "blade": (0.9, 0.35, -0.1), "cut": (0.0, -1.0, 0.0)},
        left=SHIELD_BACK, feet=FEET_PIVOT_LEFT, pivot_spin=(25.0, "Left")),
    # 振り抜き：回り終わって右足が前、右の後ろまで振り切って止める
    "MeleeY2Follow": y_pose(
        twist=-55.0, hips={"offset": (0.0, -0.04, -0.24), "lean": 12.0}, bend={"Spine": 4.0},
        right={"wrist": (-0.36, 0.12, -0.06), "elbow": (0.0, 0.3, -1.0),
               "blade": (-0.65, 0.75, -0.1), "cut": (0.75, 0.65, 0.0)},
        left=SHIELD_FORWARD, feet=FEET_RIGHT_FRONT),
})
# 2 回転の途中（1 回転目から 2 回転目へ少しずつ腰を落とす）
Y2_SPIN_STEPS = 720 // SPIN_STEP
for _i in range(1, Y2_SPIN_STEPS + 1):
    _u = _i / Y2_SPIN_STEPS
    POSES[f"MeleeY2Spin{_i}"] = spin_pose(SPIN_STEP * _i, -0.14 - 0.1 * _u, 10.0 + 4.0 * _u)

POSES.update({
    # ---- Y3：跳んで飛び込み（0〜0.7 秒）、X 字に 2 連（① 0.75 秒・② 0.9 秒） ----
    # 踏み切り：沈んで武器を右上へ振り上げる
    "MeleeY3Crouch": y_pose(
        twist=-20.0, hips={"offset": (0.0, 0.04, -0.28), "lean": 16.0}, bend={"Spine": 6.0},
        right={"wrist": (-0.1, 0.05, 0.15), "elbow": (-1.0, 0.3, 0.0),
               "blade": (-0.3, 0.5, 0.8), "cut": (0.3, -0.8, 0.4)},
        left={"wrist": (0.22, -0.1, -0.2), "elbow": (1.0, 0.3, -0.3)}, feet=FEET_BACK),
    # 跳び上がる：武器を右上へ高く掲げ、左腕は横へ開いて釣り合いを取る（スケッチ「飛び上がる」）
    "MeleeY3Jump": y_pose(
        twist=-30.0, hips={"offset": (0.0, 0.02, Y3_JUMP_HIGH), "lean": -6.0}, bend={"Spine": -4.0},
        legs=LEGS_JUMP_TUCK, feet=None,
        right={"wrist": (0.02, 0.06, 0.36), "elbow": (-1.0, 0.2, 0.2),
               "blade": (-0.2, 0.55, 0.8), "cut": (0.2, -0.8, 0.5)},
        left={"wrist": (0.38, -0.05, 0.05), "elbow": (0.0, 1.0, 0.0)}),
    # ①の振りかぶり：落ちながら右上に構える
    "MeleeY3Wind1": y_pose(
        twist=-40.0, hips={"offset": (0.0, -0.02, Y3_JUMP_HIGH * 0.35), "lean": 6.0},
        legs=LEGS_JUMP_TUCK, feet=None,
        right={"wrist": (0.0, 0.06, 0.32), "elbow": (-1.0, 0.2, 0.3),
               "blade": (-0.25, 0.7, 0.65), "cut": (0.3, -0.6, 0.7)},
        left={"wrist": (0.3, -0.15, -0.1), "elbow": (1.0, 0.3, -0.3)}),
    # ①：着地しながら右上から左下へ。当たる瞬間。左手は肩ごと後ろへ引く
    "MeleeY3Hit1": y_pose(
        twist=0.0, hips={"offset": (0.0, -0.08, -0.18), "lean": 18.0}, bend={"Spine": 8.0},
        rotate=PULL_LEFT_SHOULDER,
        right={"wrist": (0.14, -0.36, 0.06), "elbow": (-1.0, 0.0, -0.3),
               "blade": (0.35, -0.75, -0.55), "cut": (0.4, 0.3, -0.85)},
        left=SHIELD_PULL, feet=FEET_LUNGE),
    # ①の振り切り：右手は左のわき腹あたり、刃は左後ろ下へ（スケッチ「①を振り切ったあと」）
    "MeleeY3End1": y_pose(
        twist=45.0, hips={"offset": (0.0, -0.1, -0.24), "lean": 22.0}, bend={"Spine": 10.0},
        rotate=PULL_LEFT_SHOULDER,
        right={"wrist": (0.24, -0.1, -0.26), "elbow": (-1.0, -0.3, 0.0),
               "blade": (0.55, 0.5, -0.6), "cut": (0.2, 0.8, 0.4)},
        left=SHIELD_PULL, feet=FEET_LUNGE),
    # ②の振りかぶり：そのまま左上へ振り上げる
    "MeleeY3Wind2": y_pose(
        twist=40.0, hips={"offset": (0.0, -0.06, -0.18), "lean": 8.0},
        right={"wrist": (0.24, -0.08, 0.28), "elbow": (-1.0, -0.2, -0.3),
               "blade": (0.45, 0.45, 0.75), "cut": (-0.4, -0.5, 0.75)},
        left=SHIELD_BACK, feet=FEET_LUNGE),
    # ②：左上から右下へ。当たる瞬間
    "MeleeY3Hit2": y_pose(
        twist=0.0, hips={"offset": (0.0, -0.1, -0.2), "lean": 16.0}, bend={"Spine": 6.0},
        right={"wrist": (0.0, -0.38, 0.05), "elbow": (-1.0, 0.0, -0.4),
               "blade": (-0.35, -0.75, -0.55), "cut": (-0.4, 0.3, -0.85)},
        left=SHIELD_GUARD, feet=FEET_LUNGE),
    # ②の振り切り：右手は右後ろへ伸びる（スケッチ「②を振り切ったあと」）
    "MeleeY3End2": y_pose(
        twist=-50.0, hips={"offset": (0.0, -0.06, -0.22), "lean": 12.0},
        right={"wrist": (-0.36, 0.12, -0.1), "elbow": (0.0, 0.3, -1.0),
               "blade": (-0.6, 0.6, -0.5), "cut": (0.6, 0.6, 0.4)},
        left=SHIELD_FORWARD, feet=FEET_LUNGE),

    # ---- Y4：三角形に斬る。① 底辺を右回りの回転斬り（判定 0.4 秒・0.5 秒まで移動できる）→
    # ② 右上→左下（下がりながら 0.6 秒）→ ③ 左上→右下（0.8 秒） ----
    # ①の構え → 進む方向へ左足を踏み込む（この間にスティックで向きを決める）。踏み込んでから回る
    "MeleeY4Coil": spin_coil_pose(),
    "MeleeY4Wind": y_pose(
        twist=50.0, hips={"offset": (0.0, -0.06, -0.22), "lean": 12.0}, bend={"Spine": 6.0},
        right={"wrist": (0.3, -0.12, -0.1), "elbow": (0.0, 0.3, -1.0),
               "blade": (0.9, 0.35, -0.1), "cut": (0.0, -1.0, 0.0)},
        left=SHIELD_BACK, feet=FEET_PIVOT_LEFT, pivot_spin=(25.0, "Left")),
})
# ①の回転の途中（左足を軸に 1 回転）
Y4_SPIN_STEPS = 360 // SPIN_STEP
for _i in range(1, Y4_SPIN_STEPS + 1):
    POSES[f"MeleeY4Spin{_i}"] = spin_pose(SPIN_STEP * _i, -0.2, 10.0)
POSES.update({
    # ②の振りかぶり：回り終わって右足が前、右上へ振り上げる
    "MeleeY4Wind2": y_pose(
        twist=-35.0, hips={"offset": (0.0, 0.06, -0.12), "lean": 0.0},
        right={"wrist": (0.0, 0.06, 0.32), "elbow": (-1.0, 0.2, 0.3),
               "blade": (-0.25, 0.7, 0.65), "cut": (0.3, -0.6, 0.7)},
        left=SHIELD_FORWARD, feet=FEET_RIGHT_FRONT),
    # ②：右上から左下へ。左足も引き、上体を起こしながら下がって当てる
    "MeleeY4Hit2": y_pose(
        twist=10.0, hips={"offset": (0.0, 0.12, -0.1), "lean": -6.0}, bend={"Spine": -2.0, "Spine1": -2.0},
        right={"wrist": (0.16, -0.34, 0.0), "elbow": (-1.0, 0.0, -0.3),
               "blade": (0.4, -0.75, -0.5), "cut": (0.4, 0.3, -0.85)},
        left=SHIELD_BACK, feet=FEET_Y4_BACK),
    # ②の振り切り＝③の振りかぶり：左下からそのまま左上へ返す
    "MeleeY4Wind3": y_pose(
        twist=40.0, hips={"offset": (0.0, 0.14, -0.12), "lean": -4.0},
        right={"wrist": (0.24, -0.08, 0.28), "elbow": (-1.0, -0.2, -0.3),
               "blade": (0.45, 0.45, 0.75), "cut": (-0.4, -0.5, 0.75)},
        left=SHIELD_BACK, feet=FEET_Y4_BACK),
    # ③：左上から右下へ。右足を大きく引いて左足の少し後ろに合わせ、後退しながら当てる
    "MeleeY4Hit3": y_pose(
        twist=0.0, hips={"offset": (0.0, 0.16, -0.22), "lean": 18.0}, bend={"Spine": 8.0},
        right={"wrist": (0.0, -0.38, 0.04), "elbow": (-1.0, 0.0, -0.4),
               "blade": (-0.35, -0.75, -0.55), "cut": (-0.4, 0.3, -0.85)},
        left=SHIELD_GUARD, feet=FEET_Y4_CLOSE),
    # ③の振り切り（締め）：右後ろ下まで振り抜いて止める
    "MeleeY4End": y_pose(
        twist=-55.0, hips={"offset": (0.0, 0.14, -0.26), "lean": 14.0}, bend={"Spine": 4.0},
        right={"wrist": (-0.36, 0.12, -0.14), "elbow": (0.0, 0.3, -1.0),
               "blade": (-0.6, 0.55, -0.55), "cut": (0.6, 0.6, 0.4)},
        left=SHIELD_FORWARD, feet=FEET_Y4_CLOSE),
})


def spin_keys(prefix, steps, start, step_time):
    """回転のキー（1 つ目は加速、あとは等速で止まらない）"""
    return [(start + step_time * (i - 1), f"{prefix}{i}", "in" if i == 1 else "linear") for i in range(1, steps + 1)]


CLIPS.update({
    # Y1（startup 0.45 / active 0.2 / recovery 0.3）：盾を構えて突進 → 0.45 秒で突き
    "MeleeY1": {"duration": 1.15, "keys": [(0.0, "BossIdle"), (0.1, "MeleeY1Charge"), (0.38, "MeleeY1Charge"),
                                          (0.45, "MeleeY1Hit", "in"), (0.75, "MeleeY1Hit"), (1.15, "BossIdle")]},
    # Y2（判定 0.4 秒・0.9 秒、技の長さ 1.5 秒）：0.25 秒から 2 回転を止めずに（1 回転 0.36 秒）
    "MeleeY2": {"duration": 1.8, "keys": [(0.0, "BossIdle"), (0.12, "MeleeY2Coil"), (0.27, "MeleeY2Wind")]
                + spin_keys("MeleeY2Spin", Y2_SPIN_STEPS, 0.33, 0.058)
                + [(1.08, "MeleeY2Follow"), (1.35, "MeleeY2Follow"), (1.8, "BossIdle")]},
    # Y3（① 0.75 秒・② 0.9 秒、技の長さ 1.35 秒）
    "MeleeY3": {"duration": 1.75, "keys": [(0.0, "BossIdle"), (0.15, "MeleeY3Crouch"), (0.38, "MeleeY3Jump"),
                                          (0.66, "MeleeY3Wind1"), (0.75, "MeleeY3Hit1", "in"),
                                          (0.8, "MeleeY3End1"), (0.85, "MeleeY3Wind2"),
                                          (0.9, "MeleeY3Hit2", "in"), (0.98, "MeleeY3End2"),
                                          (1.3, "MeleeY3End2"), (1.75, "BossIdle")]},
    # Y4（① 0.4 秒・② 0.6 秒・③ 0.8 秒、技の長さ 1.4 秒）
    "MeleeY4": {"duration": 1.85, "keys": [(0.0, "BossIdle"), (0.1, "MeleeY4Coil"), (0.24, "MeleeY4Wind")]
                + spin_keys("MeleeY4Spin", Y4_SPIN_STEPS, 0.29, 0.042)
                + [(0.56, "MeleeY4Wind2"), (0.6, "MeleeY4Hit2", "in"), (0.72, "MeleeY4Wind3"),
                   (0.8, "MeleeY4Hit3", "in"), (0.9, "MeleeY4End"), (1.35, "MeleeY4End"),
                   (1.85, "BossIdle")]},
})

# ---- 単独の溜め＝強化（24 番 §8.2。2026-11-28 ユーザー：Y+B は近接の強化。溜めた段階で強化の量が変わる） ----
# 溜め：刃を顔の前に縦に立て、左手をかざしてエネルギーを込める（足は床のまま・震わせない）。段階 3（1.2 秒）で自動で放つ
# かける時：両腕を左右へゆっくり振り払って構え直す（releaseLock 0.85 秒の中で待機へ戻る）
FEET_BUFF = {"Right": (-0.05, 0.16), "Left": (0.03, -0.12)}   # 左足を少し前へ・右足を後ろへ開いて腰を落とす
POSES.update({
    # 刃を顔の前へ縦に立てる（刃の付いた側は前）。左手は刃の横に手のひらを向けてかざす
    "MeleeBuffRaise": melee_pose(
        twist=-15.0, hips={"offset": (0.0, 0.02, -0.08), "lean": 4.0},
        right={"wrist": (0.1, -0.3, -0.04), "elbow": (-1.0, 0.2, -0.6),
               "blade": (0.0, -0.1, 1.0), "cut": (0.0, -1.0, 0.0)},
        left={"wrist": (-0.16, -0.3, 0.12), "elbow": (1.0, 0.0, -0.6), "palm": (-1.0, 0.0, 0.0)},
        feet=FEET_BUFF),
    # 段階 3 に着くまでに、少しずつ深く構える（腰を落として左手を刃へ寄せる）
    "MeleeBuffFocus": melee_pose(
        twist=-20.0, hips={"offset": (0.0, 0.03, -0.14), "lean": 6.0}, bend={"Spine": 2.0},
        right={"wrist": (0.1, -0.3, 0.0), "elbow": (-1.0, 0.2, -0.6),
               "blade": (0.0, -0.05, 1.0), "cut": (0.0, -1.0, 0.0)},
        left={"wrist": (-0.2, -0.3, 0.16), "elbow": (1.0, 0.0, -0.5), "palm": (-1.0, 0.0, 0.0)},
        feet=FEET_BUFF),
    # 振り払い：両腕を左右の太ももの横へ下ろすように払う（右手の刃は体の右下へ、左手も同じように左下へ）。
    # 攻撃ではないのでゆっくり。開きすぎない・反らない（2026-11-28 ユーザー）
    "MeleeBuffSweep": melee_pose(
        twist=-8.0, hips={"offset": (0.0, 0.0, -0.12), "lean": 4.0},
        right={"wrist": (-0.18, -0.1, -0.38), "elbow": (-0.3, 1.0, 0.0),
               "blade": (-0.75, -0.35, -0.5), "cut": (0.0, 1.0, -0.2)},
        left={"wrist": (0.18, -0.1, -0.38), "elbow": (0.3, 1.0, 0.0), "palm": (0.0, -1.0, 0.0)},
        feet=FEET_BUFF),
})

CLIPS.update({
    # 溜めている間（ループ。段階 2 が 0.6 秒、段階 3 で自動で放つのが 1.2 秒）
    "MeleeChargeHoldSolo": {"duration": 1.5, "keys": [(0.0, "BossIdle"), (0.3, "MeleeBuffRaise"),
                                                     (1.2, "MeleeBuffFocus"), (1.5, "MeleeBuffFocus")]},
    # かける時（段階によらず同じ）
    "MeleeChargeSolo": {"duration": 0.85, "keys": [(0.0, "MeleeBuffFocus"), (0.3, "MeleeBuffSweep"),
                                                  (0.55, "MeleeBuffSweep"), (0.85, "BossIdle")]},
})

# ---- RT+Y+B の大技（24 番 §10。2026-11-28 ユーザーのスケッチ） ----
# ① 両手で銃を正面へ構えて溜める → ② 正面へ爆発、その反動で右回りに回り始める →
# ③〜⑥ 左足を軸に 1 回転（刃は斜めの面を回る：後ろから見て右が高く左が低い）→ 右の後ろ下まで振り切る。
# 刃が回るのは 1 回転＋振り切りの 120 度ほど。体は 1 回転で正面へ戻り、残りは腕と上体のひねりで振る
# 回転中の刃の上下の傾き（刃の向きの Z）。体が 180 度回った所（右上）でいちばん高く、0 度の側で低い。
# 爆発直後に右上まで高く上げる（2026-11-28 ユーザー）ので、高い側を大きく
BURST_TILT_HIGH = 0.95
BURST_TILT_LOW = 0.35
# 回転の角度と時間：爆発直後は速く右上へ → 右上で少しゆっくり → 振り下ろしながら徐々に速くして振り切る（2026-11-28 ユーザー）
# 時間は放つ瞬間（MeleeBurst の始まり）から
BURST_SPIN_KEYS = ((100, 0.19), (160, 0.25), (200, 0.37), (240, 0.48), (290, 0.56), (340, 0.61))
BURST_FOLLOW_TIME = 0.65


def burst_spin_pose(deg):
    """大技の回転の途中：Y2 の回転と同じく刃は体の左（進む側）。斜めの面を回るよう刃の高さを回転の角度で変える"""
    c = float(np.cos(np.radians(deg)))
    rise = -c * (BURST_TILT_HIGH if c < 0.0 else BURST_TILT_LOW)
    return y_pose(twist=20.0, hips={"offset": (0.0, 0.0, -0.18), "lean": 8.0}, look=False,
                  bend={"Spine": 4.0, "Spine1": 3.0},
                  right={"wrist": (0.3, -0.2, -0.04 + 0.35 * rise), "elbow": (0.0, 0.3, -1.0),
                         "blade": (1.0, -0.1, rise), "cut": (0.0, -1.0, 0.0)},
                  left=SHIELD_BACK, feet=FEET_PIVOT_LEFT, pivot_spin=(-float(deg), "Left"))


POSES.update({
    # ①：両手で銃を正面へ構えて溜める（腰を落として踏ん張る）
    "MeleeBurstAim": melee_pose(
        twist=10.0, hips={"offset": (0.0, 0.04, -0.14), "lean": 6.0}, bend={"Spine": 2.0},
        right={"wrist": (0.1, -0.4, 0.06), "elbow": (-1.0, 0.0, -0.3),
               "blade": (0.0, -1.0, 0.02), "cut": (0.0, 0.0, -1.0)},
        left=TWO_HAND, feet=FEET_BACK),
    # ②：撃った瞬間。反動で銃口が跳ね上がり、上体が後ろへ押される
    "MeleeBurstFire": melee_pose(
        twist=5.0, hips={"offset": (0.0, 0.1, -0.12), "lean": -4.0}, bend={"Spine": -3.0},
        right={"wrist": (0.08, -0.34, 0.16), "elbow": (-1.0, 0.0, -0.2),
               "blade": (0.0, -0.85, 0.5), "cut": (0.0, 0.5, -0.85)},
        left=TWO_HAND, feet=FEET_BACK),
    # 反動で右腕が右後ろへ跳ね、体が右回りに回り始める。左足を踏み出して軸にする
    "MeleeBurstRecoil": y_pose(
        twist=-30.0, hips={"offset": (0.0, 0.04, -0.16), "lean": 2.0},
        right={"wrist": (-0.3, 0.08, 0.22), "elbow": (-0.4, 1.0, 0.0),
               "blade": (-0.45, 0.6, 0.65), "cut": (0.8, 0.5, 0.0)},
        left=SHIELD_BACK, feet=FEET_PIVOT_LEFT, pivot_spin=(-40.0, "Left")),
    # ⑥：1 回転して正面へ戻り、腕と上体で右の後ろ下まで振り切る。右足が前
    "MeleeBurstFollow": y_pose(
        twist=-65.0, hips={"offset": (0.0, -0.04, -0.26), "lean": 12.0}, bend={"Spine": 4.0},
        right={"wrist": (-0.34, 0.14, -0.2), "elbow": (0.0, 0.3, -1.0),
               "blade": (-0.55, 0.55, -0.65), "cut": (0.6, 0.6, 0.4)},
        left=SHIELD_FORWARD, feet=FEET_RIGHT_FRONT),
})
for _deg, _t in BURST_SPIN_KEYS:
    POSES[f"MeleeBurstSpin{_deg}"] = burst_spin_pose(_deg)

CLIPS.update({
    # 溜めている間（ループ。RT+Y+B を押している長さで威力の段階が上がり、1.2 秒で自動で放つ）
    "MeleeChargeHoldBurst": {"duration": 1.5, "keys": [(0.0, "BossIdle"), (0.15, "MeleeBurstAim"),
                                                      (1.5, "MeleeBurstAim")]},
    # 放つ：0.05 秒で爆発 → 0.13〜0.61 秒に 1 回転 → 0.65 秒に振り切り
    # （MeleeCombo.json の charge.burst：爆発の判定 0.05 秒、回転斬りの判定は 0.19〜0.65 秒に最大 5 回）
    "MeleeBurst": {"duration": 1.55, "keys": [(0.0, "MeleeBurstAim"),
                                             (0.05, "MeleeBurstFire", "in"), (0.13, "MeleeBurstRecoil", "linear")]
                   + [(_t, f"MeleeBurstSpin{_d}", "linear") for _d, _t in BURST_SPIN_KEYS]
                   + [(BURST_FOLLOW_TIME, "MeleeBurstFollow", "linear"), (1.05, "MeleeBurstFollow"),
                      (1.55, "BossIdle")]},
})
