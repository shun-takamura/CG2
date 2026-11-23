#pragma once
#include "MathUtility.h"

// クオータニオン（回転を表す4要素）
struct Quaternion {
    float x, y, z, w;
};

// =====================================
// クオータニオンの基本演算
// =====================================

/// <summary>単位クオータニオン（回転なし）</summary>
/// <returns>(0, 0, 0, 1)</returns>
Quaternion IdentityQuaternion();

/// <summary>長さ 1 にする</summary>
/// <param name="q">クオータニオン</param>
/// <returns>正規化したクオータニオン（長さ 0 なら単位クオータニオン）</returns>
Quaternion Normalize(const Quaternion& q);

/// <summary>4 成分の内積（Slerp で使う）</summary>
/// <param name="a">左辺</param>
/// <param name="b">右辺</param>
/// <returns>内積</returns>
float Dot(const Quaternion& a, const Quaternion& b);

/// <summary>クオータニオン同士の積（q1 * q2。q2 の回転の後に q1 の回転をかける合成）</summary>
/// <param name="q1">左辺</param>
/// <param name="q2">右辺</param>
/// <returns>積</returns>
Quaternion Multiply(const Quaternion& q1, const Quaternion& q2);

// =====================================
// 補間
// =====================================

/// <summary>球面線形補間。内積が負なら最短経路になるよう q2 を反転し、ほぼ同じ向きなら線形補間して正規化する</summary>
/// <param name="q1">t=0 の回転</param>
/// <param name="q2">t=1 の回転</param>
/// <param name="t">進み具合（0.0〜1.0）</param>
/// <returns>補間した回転</returns>
Quaternion Slerp(const Quaternion& q1, const Quaternion& q2, float t);

// =====================================
// 生成
// =====================================

/// <summary>軸と角度からクオータニオンを作る（軸は内部で正規化する）</summary>
/// <param name="axis">回転軸</param>
/// <param name="angle">回転角 [ラジアン]</param>
/// <returns>クオータニオン</returns>
Quaternion MakeRotateAxisAngleQuaternion(const Vector3& axis, float angle);

/// <summary>オイラー角（Z*Y*X 順）からクオータニオンを作る。MakeRotateMatrix(Vector3) と同じ合成順</summary>
/// <param name="euler">オイラー角 [ラジアン]</param>
/// <returns>クオータニオン</returns>
Quaternion QuaternionFromEuler(const Vector3& euler);

/// <summary>
/// クオータニオンをオイラー角（x=pitch / y=yaw / z=roll）にする。
/// MakeRotateMatrix(Vector3)=Rz*Ry*Rx の逆抽出。yaw=±90°近傍(ジンバル)では roll=0 に固定
/// </summary>
/// <param name="q">クオータニオン（正規化済み）</param>
/// <returns>オイラー角 [ラジアン]</returns>
Vector3 EulerFromQuaternion(const Quaternion& q);

// =====================================
// 行列との連携
// =====================================

/// <summary>クオータニオンから回転行列を作る（行ベクトル規約）</summary>
/// <param name="q">クオータニオン（正規化済み）</param>
/// <returns>回転行列</returns>
Matrix4x4 MakeRotateMatrix(const Quaternion& q);

/// <summary>拡縮 → 回転 → 平行移動の順のアフィン変換行列を作る（Transform 版の MakeAffineMatrix のクオータニオン版）</summary>
/// <param name="scale">拡縮</param>
/// <param name="rotate">回転</param>
/// <param name="translate">平行移動</param>
/// <returns>アフィン変換行列</returns>
Matrix4x4 MakeAffineMatrix(const Vector3& scale, const Quaternion& rotate, const Vector3& translate);