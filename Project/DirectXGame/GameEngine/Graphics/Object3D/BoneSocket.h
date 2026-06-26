#pragma once
#include "Transform.h"
#include "Matrix4x4.h"
#include "Vector3.h"
#include <string>

class AnimatedObject3DInstance;

/// <summary>
/// AnimatedObject3DInstance の名前付きジョイントに、オフセット付きで追従するソケット。
/// 武器の手持ち・手から出すエフェクトの発生点など「ボーンに何かをくっつける」用途を共通化する。
/// target は参照のみ（所有しない）。毎フレーム World() / Position() を呼んで使う。
/// </summary>
struct BoneSocket {
    AnimatedObject3DInstance* target = nullptr;  // 追従先（参照のみ）
    std::string jointName;                       // 例: "mixamorig:RightHand"
    // マウントオフセット（握り位置・向き・スケール補正）。剣モデルを握り原点で作れば単位に近づく。
    Transform   offset{ {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f} };

    /// <summary>
    /// 追従先ボーンのスケール除去済みワールド × offset。target が無ければ offset のみ。
    /// </summary>
    Matrix4x4 World() const;

    /// <summary>
    /// World() の平行移動成分。パーティクル発生点・銃口など位置だけ欲しい時に使う。
    /// </summary>
    Vector3 Position() const;
};
