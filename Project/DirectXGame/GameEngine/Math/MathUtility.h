#pragma once
#include "Matrix3x3.h"
#include "Matrix4x4.h"
#include "Vector3.h"
#include "Vector4.h"
#include "Transform.h"
#include "TransformationMatrix.h"

//===================================
// 角度変換ヘルパー
//   内部表現・保存はラジアンのまま。人間が触れるUI層でのみ度数⇔ラジアンを変換する。
//===================================
// 円周率（半周 [rad]）
inline constexpr float kPi = 3.14159265358979323846f;
// 一周 [rad]
inline constexpr float kTwoPi = 2.0f * kPi;

/// <summary>度をラジアンにする</summary>
/// <param name="deg">角度 [度]</param>
/// <returns>角度 [ラジアン]</returns>
inline constexpr float DegToRad(float deg) { return deg * (kPi / 180.0f); }
/// <summary>ラジアンを度にする</summary>
/// <param name="rad">角度 [ラジアン]</param>
/// <returns>角度 [度]</returns>
inline constexpr float RadToDeg(float rad) { return rad * (180.0f / kPi); }

/// <summary>3 軸まとめて度をラジアンにする</summary>
/// <param name="deg">角度 [度]</param>
/// <returns>角度 [ラジアン]</returns>
inline Vector3 DegToRad(const Vector3& deg) { return { DegToRad(deg.x), DegToRad(deg.y), DegToRad(deg.z) }; }
/// <summary>3 軸まとめてラジアンを度にする</summary>
/// <param name="rad">角度 [ラジアン]</param>
/// <returns>角度 [度]</returns>
inline Vector3 RadToDeg(const Vector3& rad) { return { RadToDeg(rad.x), RadToDeg(rad.y), RadToDeg(rad.z) }; }

//===================================
// MT3でも使う関数の宣言
//===================================

/// <summary>
/// 余接（1 / tan）を求める
/// </summary>
/// <param name="theta">角度 [ラジアン]</param>
/// <returns>cotangent</returns>
float Cotangent(float theta);

/// <summary>
/// 4x4 行列の積 matrix1 * matrix2 を求める（行ベクトル規約なので matrix1 の変換が先にかかる）
/// </summary>
/// <param name="matrix1">1つ目の行列</param>
/// <param name="matrix2">2つ目の行列</param>
/// <returns>4x4行列の積</returns>
Matrix4x4 Multiply(Matrix4x4 matrix1, Matrix4x4 matrix2);

/// <summary>
/// 4x4 単位行列を作る
/// </summary>
/// <returns>4x4単位行列</returns>
Matrix4x4 MakeIdentity4x4();

/// <summary>
/// 点を行列で変換し、w で割って同次座標から戻す（w が 0 なら assert）
/// </summary>
/// <param name="vector3">変換する点</param>
/// <param name="matrix4x4">変換行列</param>
/// <returns>変換後の点（デカルト座標）</returns>
Vector3 TransformCoordinate(const Vector3& vector3, const Matrix4x4& matrix4x4);

/// <summary>
/// 拡縮 → 回転（Rx·Ry·Rz）→ 平行移動の順のアフィン行列を作る（カメラ・Object3D と同じ回転順）
/// </summary>
/// <param name="transform">拡縮・回転 [ラジアン]・平行移動</param>
/// <returns>アフィン行列</returns>
Matrix4x4 MakeAffineMatrix(const Transform& transform);

/// <summary>
/// 透視投影行列を作る（左手系・深度 [0,1]）
/// </summary>
/// <param name="fovY">縦の画角 [ラジアン]</param>
/// <param name="aspectRatio">アスペクト比</param>
/// <param name="nearClip">近平面への距離</param>
/// <param name="farClip">遠平面への距離</param>
/// <returns>透視投影行列</returns>
Matrix4x4 MakePerspectiveFovMatrix(float fovY, float aspectRatio, float nearClip, float farClip);

/// <summary>
/// 正射影行列を作る（深度 [0,1]）
/// </summary>
/// <param name="left">左側の座標</param>
/// <param name="top">上側の座標</param>
/// <param name="right">右側の座標</param>
/// <param name="bottom">下側の座標</param>
/// <param name="nearClip">近平面への距離</param>
/// <param name="farClip">遠平面への距離</param>
/// <returns>正射影行列</returns>
Matrix4x4 MakeOrthographicMatrix(float left, float top, float right, float bottom, float nearClip, float farClip);

/// <summary>
/// 左手系のビュー行列（LookAt）を作成する。
/// </summary>
/// <param name="eye">視点（ワールド座標）</param>
/// <param name="target">注視点（ワールド座標）</param>
/// <param name="up">上方向</param>
/// <returns>ワールド→ビューの変換行列</returns>
Matrix4x4 MakeLookAtMatrix(const Vector3& eye, const Vector3& target, const Vector3& up);

/// <summary>
/// 4x4逆行列を求める関数
/// </summary>
/// <param name="matrix4x4">逆行列を求めたい行列</param>
/// <returns>4x4逆行列</returns>
Matrix4x4 Inverse(Matrix4x4 matrix4x4);

/// <summary>
/// 転置行列を求める関数
/// </summary>
/// <param name="matrix">転置行列を求めたい行列</param>
/// <returns>4x4転置行列</returns>
Matrix4x4 Transpose(const Matrix4x4& matrix);

/// <summary>拡縮行列を作る</summary>
/// <param name="transform">scale だけを使う</param>
/// <returns>拡縮行列</returns>
Matrix4x4 MakeScaleMatrix(Transform transform);

/// <summary>Rz·Ry·Rx 順の回転行列を作る（カメラの基底には MakeCameraRotateMatrix を使う）</summary>
/// <param name="rotate">各軸の回転角 [ラジアン]</param>
/// <returns>回転行列</returns>
Matrix4x4 MakeRotateMatrix(Vector3 rotate);

/// <summary>X 軸まわりの回転行列を作る</summary>
/// <param name="rotate">x だけを使う [ラジアン]</param>
/// <returns>回転行列</returns>
Matrix4x4 MakeRotateXMatrix(Vector3 rotate);

/// <summary>Y 軸まわりの回転行列を作る</summary>
/// <param name="rotate">y だけを使う [ラジアン]</param>
/// <returns>回転行列</returns>
Matrix4x4 MakeRotateYMatrix(Vector3 rotate);

/// <summary>Z 軸まわりの回転行列を作る</summary>
/// <param name="rotate">z だけを使う [ラジアン]</param>
/// <returns>回転行列</returns>
Matrix4x4 MakeRotateZMatrix(Vector3 rotate);

/// <summary>
/// カメラのワールド行列（Camera::Update → MakeAffineMatrix）と同じ Rx·Ry·Rz 順の回転行列を作る。
/// MakeRotateMatrix(Vector3) は Rz·Ry·Rx なので、ピッチとヨーが同時に掛かる姿勢では描画上のカメラ基底（右/上/前）とずれる。
/// カメラの基底が欲しいときはこちらを使う
/// </summary>
/// <param name="rotate">各軸の回転角 [ラジアン]</param>
/// <returns>回転行列</returns>
Matrix4x4 MakeCameraRotateMatrix(const Vector3& rotate);

/// <summary>平行移動行列を作る</summary>
/// <param name="transform">translate だけを使う</param>
/// <returns>平行移動行列</returns>
Matrix4x4 MakeTranslateMatrix(Transform transform);

/// <summary>点を行列で変換し、w で割る（TransformCoordinate と同じ。w が 0 なら assert）</summary>
/// <param name="vector">変換する点</param>
/// <param name="matrix">変換行列</param>
/// <returns>変換後の点</returns>
Vector3 TransformMatrix(const Vector3& vector, const Matrix4x4& matrix);

/// <summary>3 次元ベクトルの内積</summary>
/// <param name="a">左辺</param>
/// <param name="b">右辺</param>
/// <returns>内積</returns>
float Dot(const Vector3& a, const Vector3& b);

/// <summary>ベクトルの長さ</summary>
/// <param name="vector">ベクトル</param>
/// <returns>長さ</returns>
float Length(const Vector3& vector);

/// <summary>長さ 1 にする</summary>
/// <param name="vector">ベクトル</param>
/// <returns>正規化したベクトル（長さ 0 のベクトルは向きが決まらないので 0 ベクトル）</returns>
Vector3 Normalize(const Vector3& vector);

/// <summary>線形補間（v1 + (v2 - v1) * t）</summary>
/// <param name="v1">t=0 の値</param>
/// <param name="v2">t=1 の値</param>
/// <param name="t">進み具合（クランプしない）</param>
/// <returns>補間した値</returns>
Vector3 Lerp(const Vector3& v1, const Vector3& v2, float t);