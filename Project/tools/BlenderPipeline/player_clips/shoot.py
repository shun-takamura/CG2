"""まとまり shoot（C 射撃）：通常の射撃、チャージ中（ループ）、チャージを放つ、精密射撃の構え。

鳴らすのは PlayerAnimPart（Resources/Json/Tuning/PlayerAnim/shoot.json の shoot / chargeHold / chargeRelease / precisionAim）。
STG と地上で脚の形が違う（1 本のクリップが体全体を書く）ので、STG 用は ...Rail の名前で別に作り、
shoot.json の railClip / groundClip で分ける（2026-11-27 ユーザー了承）。chargeHold は地上だけ（STG は滑空 railGlide）。
書き方は 25_PlayerAnimClips.md §3。

武器は右手（Hand のボーンの向き＝指先の向きが銃身。MainWeapon.json の grip）、盾は左手。銃口は手に付いてくるので、
弾の出る位置は腕の向きで変わる。銃身はゲームの正面（アーマチュアの −Y）へ向ける。
腕の向きは元の姿勢の tilt（STG の Fly は 40° 前へ倒れる）の後で付けたいので、"arms" ではなく call（いちばん最後）で付ける。
"""

import importlib

# STG の元の姿勢：B（move.py）の RailStand があればそれ、まだ無ければ今の STG の姿勢（Launch の最後＝Fly）
try:
    RAIL_BASE = "RailStand" if "RailStand" in importlib.import_module("player_clips.move").POSES else "Fly"
except ImportError:
    RAIL_BASE = "Fly"
GROUND_BASE = "BossIdle"

# 正面（アーマチュアの空間。正面は −Y、+X がモデルの左、+Z が上）
FORWARD = (0.0, -1.0, 0.0)


def aim(right=None, left=None):
    """腕の向きを最後に付ける call を作る。right / left は {"Arm"/"ForeArm"/"Hand": 向き}（どちらも右側の書き方）

    Args:
        right: 右腕（武器）の向き
        left: 左腕（盾）の向き
    Returns:
        pose_body の call に渡す関数 f(arm, rig, phase)
    """
    def apply(arm, rig, phase):
        for side, dirs in (("Right", right), ("Left", left)):
            if not dirs:
                continue
            for link in ("Arm", "ForeArm", "Hand"):
                d = dirs.get(link)
                if d:
                    rig.pose_bone_toward(arm, rig.MX + side + link, rig.side_vec(d, side))
    return apply


# ----- 腕の向き（右側の書き方。右は −X が外）-----
# 構え：右腕を肩の高さで前へ伸ばす。ひじを少しだけ外へ逃がす
RIGHT_AIM = {"Arm": (-0.12, -1.0, -0.08), "ForeArm": (0.04, -1.0, 0.0), "Hand": FORWARD}
# 撃った反動：前腕と手が上へ跳ね、肩が少し引ける
RIGHT_RECOIL = {"Arm": (-0.12, -1.0, 0.02), "ForeArm": (0.04, -1.0, 0.3), "Hand": (0.0, -1.0, 0.42)}
# チャージを放つ反動：腕が跳ね上がる（ビームの根元は銃口に付いてくるので、腕は上げすぎず上体の反りで大きく見せる）
RIGHT_BIG_RECOIL = {"Arm": (-0.12, -1.0, 0.18), "ForeArm": (0.02, -0.9, 0.5), "Hand": (0.0, -0.8, 0.62)}
# 溜め：銃を斜め下へ下げ、腰の横で構える
RIGHT_CHARGE = {"Arm": (-0.3, -0.35, -1.0), "ForeArm": (0.1, -1.0, -0.45), "Hand": (0.1, -1.0, -0.7)}
# 精密射撃：腕をまっすぐ伸ばし、肩ごと目の高さへ上げて銃を目の前に置く
RIGHT_PRECISION = {"Arm": (0.12, -1.0, 0.28), "ForeArm": (0.1, -1.0, 0.05), "Hand": FORWARD}

# 盾（左腕）：力を抜いて肩からまっすぐ垂らす。上体を左へひねると左肩は後ろへ下がるので、上腕は前へ出さず
# わずかに後ろへ。ひじは自然に少しだけ曲げる（2026-11-27 ユーザー：腕が肩より前に来て力んで見える）
LEFT_GUARD = {"Arm": (-0.12, 0.08, -1.0), "ForeArm": (-0.04, -0.3, -1.0), "Hand": (-0.04, -0.3, -1.0)}
# 反動のとき：腕が少し後ろへ振れる
LEFT_LOW = {"Arm": (-0.14, 0.2, -1.0), "ForeArm": (-0.04, -0.15, -1.0), "Hand": (-0.04, -0.15, -1.0)}
# 精密射撃：構えと同じ
LEFT_PRECISION = LEFT_GUARD

# ----- 上体（背骨の向き）-----
# 右肩を前へ入れるように上体を左へひねり（+Z まわりが左）、右の鎖骨も前へ回す。首は逆へ戻して顔は正面
# （2026-11-27 ユーザー：もっと右肩を前に）
TWIST_AIM = [("Spine", (0.0, 0.0, 1.0), 8.0), ("Spine1", (0.0, 0.0, 1.0), 14.0),
             ("RightShoulder", (0.0, 0.0, 1.0), 12.0), ("Neck", (0.0, 0.0, 1.0), -22.0)]
TWIST_PRECISION = [("Spine", (0.0, 0.0, 1.0), 10.0), ("Spine1", (0.0, 0.0, 1.0), 22.0),
                   ("RightShoulder", (0.0, 0.0, 1.0), 15.0), ("Neck", (0.0, 0.0, 1.0), -28.0)]
# 反動で上体が後ろへ（bend の負が後ろ。首は逆へ起こして顔は前を向いたまま）
BEND_RECOIL = {"Spine1": -3.0, "Spine2": -3.0}
BEND_BIG_RECOIL = {"Spine": -6.0, "Spine1": -10.0, "Spine2": -10.0, "Neck": 10.0, "Head": 6.0}
# 精密射撃：上体を少し前へ入れ、頭を下げて腕の上から狙う
BEND_PRECISION = {"Spine1": 3.0, "Spine2": 3.0, "Neck": 6.0, "Head": 8.0}


def pose_pair(name, right, left, extra=None):
    """地上用（name）と STG 用（name + "Rail"）の 2 つの姿勢を作る

    Args:
        name: 地上用の姿勢の名前
        right: 右腕の向き
        left: 左腕の向き
        extra: 両方に足す項目（rotate / bend など）
    Returns:
        {姿勢の名前: 姿勢}
    """
    common = {"call": aim(right, left), "sway": {}, **(extra or {})}
    return {
        name: {"base": GROUND_BASE, **common},
        name + "Rail": {"base": RAIL_BASE, **common},
    }


POSES = {}
POSES.update(pose_pair("ShootAim", RIGHT_AIM, LEFT_GUARD, {"rotate": TWIST_AIM}))
POSES.update(pose_pair("ShootRecoil", RIGHT_RECOIL, LEFT_GUARD, {"rotate": TWIST_AIM, "bend_add": BEND_RECOIL}))
POSES.update(pose_pair("ChargeReleaseKick", RIGHT_BIG_RECOIL, LEFT_LOW,
                       {"rotate": TWIST_AIM, "bend_add": BEND_BIG_RECOIL}))
POSES.update(pose_pair("PrecisionAimPose", RIGHT_PRECISION, LEFT_PRECISION,
                       {"rotate": TWIST_PRECISION, "bend_add": BEND_PRECISION}))
# 溜め（地上だけ）：腰をさらに落とし、銃を下げて力をためる。わずかに上下する
POSES["ChargeHoldPose"] = {
    "base": GROUND_BASE,
    "hips": {"offset": (0.0, 0.03, -0.14), "lean": 10.0},
    "call": aim(RIGHT_CHARGE, LEFT_GUARD),
    "sway": {"hips_z": 0.006, "spine": 0.8},
}


def add_bend(poses):
    """"bend_add"（元の姿勢の bend に足す角度）を bend にまとめる。base は項目ごとの置き換えなので、bend を書くと
    元の姿勢の bend が消える。そこで元の姿勢（move / start の表）の bend を写して、足す分を上乗せする

    Args:
        poses: 姿勢の表（その場で書き換える）
    """
    base_bend = {}
    for group in ("move", "start"):
        try:
            module = importlib.import_module("player_clips." + group)
        except ImportError:
            continue
        for name, spec in module.POSES.items():
            base_bend.setdefault(name, spec)
    for spec in poses.values():
        add = spec.pop("bend_add", None)
        if not add:
            continue
        # base をたどって bend を探す（base の base まで）
        bend, base = {}, spec.get("base")
        while base in base_bend:
            if "bend" in base_bend[base]:
                bend = dict(base_bend[base]["bend"])
                break
            base = base_bend[base].get("base")
        for bone, deg in add.items():
            bend[bone] = bend.get(bone, 0.0) + deg
        spec["bend"] = bend


add_bend(POSES)


CLIPS = {}
for suffix in ("", "Rail"):
    aim_pose, recoil, kick = "ShootAim" + suffix, "ShootRecoil" + suffix, "ChargeReleaseKick" + suffix
    # 通常の射撃：構えから始まり（元の姿勢からはフェードで構える）、跳ねて構えに戻り、構えたまま終わる。
    # 連射（0.3 秒おき）で鳴り直しても構えのまま。撃つのをやめると元の姿勢へフェードで戻る
    CLIPS["Shoot" + suffix] = {"duration": 0.45, "keys": [(0.0, aim_pose), (0.04, recoil, "linear"),
                                                          (0.22, aim_pose), (0.45, aim_pose)]}
    # チャージを放つ：大きく跳ね上がって、ゆっくり構えへ戻る
    CLIPS["ChargeRelease" + suffix] = {"duration": 0.8, "keys": [(0.0, aim_pose), (0.06, kick, "linear"),
                                                                 (0.5, aim_pose), (0.8, aim_pose)]}
    # 精密射撃の構え（押している間のループ・動かない）
    CLIPS["PrecisionAim" + suffix] = {"duration": 1.0, "pose": "PrecisionAimPose" + suffix}
# チャージ中（地上だけのループ）
CLIPS["ChargeHold"] = {"duration": 1.2, "pose": "ChargeHoldPose"}
