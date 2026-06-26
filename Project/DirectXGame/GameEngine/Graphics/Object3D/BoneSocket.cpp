#include "BoneSocket.h"
#include "AnimatedObject3DInstance.h"
#include "MathUtility.h"

Matrix4x4 BoneSocket::World() const
{
    const Matrix4x4 mount = MakeAffineMatrix(offset);
    if (!target) return mount;
    // GetJointSocketMatrix はボーンのスケール（mixamo の cm→m 等）を除去した
    // 回転＋位置のみの行列を返す。offset を手前に掛けてマウントする。
    return Multiply(mount, target->GetJointSocketMatrix(jointName));
}

Vector3 BoneSocket::Position() const
{
    const Matrix4x4 w = World();
    return { w.m[3][0], w.m[3][1], w.m[3][2] };
}
