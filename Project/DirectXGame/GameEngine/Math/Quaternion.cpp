#include "Quaternion.h"
#include <cmath>

Quaternion IdentityQuaternion()
{
    return { 0.0f, 0.0f, 0.0f, 1.0f };
}

Quaternion Normalize(const Quaternion& q)
{
    float length = sqrtf(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    if (length == 0.0f) {
        return IdentityQuaternion();
    }
    return { q.x / length, q.y / length, q.z / length, q.w / length };
}

float Dot(const Quaternion& a, const Quaternion& b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
}

Quaternion Multiply(const Quaternion& q1, const Quaternion& q2)
{
    Quaternion result;
    result.x = q1.w * q2.x + q1.x * q2.w + q1.y * q2.z - q1.z * q2.y;
    result.y = q1.w * q2.y - q1.x * q2.z + q1.y * q2.w + q1.z * q2.x;
    result.z = q1.w * q2.z + q1.x * q2.y - q1.y * q2.x + q1.z * q2.w;
    result.w = q1.w * q2.w - q1.x * q2.x - q1.y * q2.y - q1.z * q2.z;
    return result;
}

Quaternion Slerp(const Quaternion& q1, const Quaternion& q2, float t)
{
    // 内積を計算
    float dot = Dot(q1, q2);

    // 内積が負なら片方を反転して「最短経路」で補間する
    Quaternion q2Adjusted = q2;
    if (dot < 0.0f) {
        q2Adjusted = { -q2.x, -q2.y, -q2.z, -q2.w };
        dot = -dot;
    }

    // ほぼ同じ向きなら線形補間にフォールバック（ゼロ除算を避ける）
    const float epsilon = 0.0005f;
    if (dot >= 1.0f - epsilon) {
        Quaternion result = {
            (1.0f - t) * q1.x + t * q2Adjusted.x,
            (1.0f - t) * q1.y + t * q2Adjusted.y,
            (1.0f - t) * q1.z + t * q2Adjusted.z,
            (1.0f - t) * q1.w + t * q2Adjusted.w
        };
        return Normalize(result);
    }

    // 球面線形補間
    float theta = acosf(dot);
    float sinTheta = sinf(theta);
    float scale1 = sinf((1.0f - t) * theta) / sinTheta;
    float scale2 = sinf(t * theta) / sinTheta;

    return {
        scale1 * q1.x + scale2 * q2Adjusted.x,
        scale1 * q1.y + scale2 * q2Adjusted.y,
        scale1 * q1.z + scale2 * q2Adjusted.z,
        scale1 * q1.w + scale2 * q2Adjusted.w
    };
}

Quaternion MakeRotateAxisAngleQuaternion(const Vector3& axis, float angle)
{
    Vector3 normalizedAxis = Normalize(axis);
    float halfAngle = angle / 2.0f;
    float halfSin = sinf(halfAngle);
    float halfCos = cosf(halfAngle);
    return {
        normalizedAxis.x * halfSin,
        normalizedAxis.y * halfSin,
        normalizedAxis.z * halfSin,
        halfCos
    };
}

Matrix4x4 MakeRotateMatrix(const Quaternion& q)
{
    Matrix4x4 result;

    float xx = q.x * q.x;
    float yy = q.y * q.y;
    float zz = q.z * q.z;
    float xy = q.x * q.y;
    float xz = q.x * q.z;
    float yz = q.y * q.z;
    float wx = q.w * q.x;
    float wy = q.w * q.y;
    float wz = q.w * q.z;

    result.m[0][0] = 1.0f - 2.0f * (yy + zz);
    result.m[0][1] = 2.0f * (xy + wz);
    result.m[0][2] = 2.0f * (xz - wy);
    result.m[0][3] = 0.0f;

    result.m[1][0] = 2.0f * (xy - wz);
    result.m[1][1] = 1.0f - 2.0f * (xx + zz);
    result.m[1][2] = 2.0f * (yz + wx);
    result.m[1][3] = 0.0f;

    result.m[2][0] = 2.0f * (xz + wy);
    result.m[2][1] = 2.0f * (yz - wx);
    result.m[2][2] = 1.0f - 2.0f * (xx + yy);
    result.m[2][3] = 0.0f;

    result.m[3][0] = 0.0f;
    result.m[3][1] = 0.0f;
    result.m[3][2] = 0.0f;
    result.m[3][3] = 1.0f;

    return result;
}

Matrix4x4 MakeAffineMatrix(const Vector3& scale, const Quaternion& rotate, const Vector3& translate)
{
    // スケール行列
    Matrix4x4 scaleMatrix = {};
    scaleMatrix.m[0][0] = scale.x;
    scaleMatrix.m[1][1] = scale.y;
    scaleMatrix.m[2][2] = scale.z;
    scaleMatrix.m[3][3] = 1.0f;

    // 回転行列（Quaternionから）
    Matrix4x4 rotateMatrix = MakeRotateMatrix(rotate);

    // 平行移動行列
    Matrix4x4 translateMatrix = MakeIdentity4x4();
    translateMatrix.m[3][0] = translate.x;
    translateMatrix.m[3][1] = translate.y;
    translateMatrix.m[3][2] = translate.z;

    // 拡縮 → 回転 → 平行移動 の順
    return Multiply(Multiply(scaleMatrix, rotateMatrix), translateMatrix);
}