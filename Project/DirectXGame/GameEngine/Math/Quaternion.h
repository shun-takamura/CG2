#pragma once
#include "MathUtility.h"

// クオータニオン（回転を表す4要素）
struct Quaternion {
    float x, y, z, w;
};

// =====================================
// クオータニオンの基本演算
// =====================================

// 単位クオータニオン（回転なし）を返す
Quaternion IdentityQuaternion();

// 正規化
Quaternion Normalize(const Quaternion& q);

// 内積（Slerpで使う）
float Dot(const Quaternion& a, const Quaternion& b);

// クオータニオン同士の掛け算（合成回転）
Quaternion Multiply(const Quaternion& q1, const Quaternion& q2);

// =====================================
// 補間
// =====================================

// 球面線形補間
Quaternion Slerp(const Quaternion& q1, const Quaternion& q2, float t);

// =====================================
// 生成
// =====================================

// 軸と角度からクオータニオンを生成
Quaternion MakeRotateAxisAngleQuaternion(const Vector3& axis, float angle);

// =====================================
// 行列との連携
// =====================================

// クオータニオンから回転行列を生成
Matrix4x4 MakeRotateMatrix(const Quaternion& q);

// アフィン変換行列を生成（Quaternion版）
// 既存のMakeAffineMatrix(const Transform&)とはオーバーロードで共存
Matrix4x4 MakeAffineMatrix(const Vector3& scale, const Quaternion& rotate, const Vector3& translate);