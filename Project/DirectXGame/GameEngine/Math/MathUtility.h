#pragma once
#include "Matrix3x3.h"
#include "Matrix4x4.h"
#include "Vector3.h"
#include "Vector4.h"
#include "Transform.h"
#include "TransformationMatrix.h"

//===================================
// MT3でも使う関数の宣言
//===================================

/// <summary>
/// cotangent(余接)を求める関数
/// </summary>
/// <param name="theta">θ(シータ)</param>
/// <returns>cotangent</returns>
float Cotangent(float theta);

/// <summary>
/// 4x4行列の積を求める関数
/// </summary>
/// <param name="matrix1">1つ目の行列</param>
/// <param name="matrix2">1つ目の行列</param>
/// <returns>4x4行列の積</returns>
Matrix4x4 Multiply(Matrix4x4 matrix1, Matrix4x4 matrix2);

/// <summary>
/// 4x4単位行列作成関数
/// </summary>
/// <returns>4x4単位行列</returns>
Matrix4x4 MakeIdentity4x4();

/// <summary>
/// 座標系変換関数
/// </summary>
/// <param name="vector3"></param>
/// <param name="matrix4x4"></param>
/// <returns>デカルト座標系</returns>
Vector3 TransformCoordinate(const Vector3& vector3, const Matrix4x4& matrix4x4);

/// <summary>
/// アフィン行列作成関数
/// </summary>
/// <param name="scale">縮尺</param>
/// <param name="rotate">thetaを求めるための数値</param>
/// <param name="translate">三次元座標でのx,y,zの移動量</param>
/// <returns>アフィン行列</returns>
Matrix4x4 MakeAffineMatrix(const Transform& transform);

/// <summary>
/// 投視投影行列作成関数
/// </summary>
/// <param name="fovY">縦の画角</param>
/// <param name="aspectRatio">アスペクト比</param>
/// <param name="nearClip">近平面への距離</param>
/// <param name="farClip">遠平面への距離</param>
/// <returns>投視投影行列</returns>
Matrix4x4 MakePerspectiveFovMatrix(float fovY, float aspectRatio, float nearClip, float farClip);

/// <summary>
/// 正射影行列作関数
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

// Scale行列作成関数
Matrix4x4 MakeScaleMatrix(Transform transform);

// X回転行列作成関数
Matrix4x4 MakeRotateXMatrix(Vector3 rotate);

// Y回転行列作成関数
Matrix4x4 MakeRotateYMatrix(Vector3 rotate);

// Z回転行列作成関数
Matrix4x4 MakeRotateZMatrix(Vector3 rotate);

// 移動行列作成関数
Matrix4x4 MakeTranslateMatrix(Transform transform);

Vector3 TransformMatrix(const Vector3& vector, const Matrix4x4& matrix);