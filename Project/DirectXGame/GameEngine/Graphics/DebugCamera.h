#pragma once
#include "Vector3.h"
#include "Matrix4x4.h"
#include <cassert>
#include <math.h>

#define DIRECTINPUT_VERSION 0x0800 // Directinputのバージョン指定
#include <dinput.h> // 必ずバージョン指定後にインクルード
#pragma comment(lib,"dinput8.lib")
#pragma comment(lib,"dxguid.lib")

/// <summary>
/// デバッグカメラ
/// </summary>
class DebugCamera
{
private:

	//=======================
	// 関数の宣言
	//=======================
	Vector3 Add(const Vector3& v1, const Vector3& v2);

	Matrix4x4 Multiply(Matrix4x4 matrix1, Matrix4x4 matrix2);

	Matrix4x4 MakeRotateXMatrix(Vector3 rotate);

	Matrix4x4 MakeRotateYMatrix(Vector3 rotate);

	Matrix4x4 MakeRotateZMatrix(Vector3 rotate);

	Matrix4x4 MakeRotateMatrix(const Vector3& rotation);

	Matrix4x4 MakeTranslationMatrix(Vector3 translation);

	Matrix4x4 Inverse(Matrix4x4 matrix4x4);

	Matrix4x4 MakeIdentity4x4();

	Vector3 Multiply(const Vector3& vec, const Matrix4x4& mat);

	Vector3 Transform(const Vector3& vec, const Vector3& rotation);
	

	//=========================
	// ローカル変数の宣言
	//=========================
	// X,Y,Z軸回りのローカル回転角
	//Vector3 rotation_ = { 0.0f,0.0f,0.0f };

	float distance_ = 50.0f; // ピボットからの距離

	// 累積回転行列
	Matrix4x4 matRot_ = MakeIdentity4x4();

	// 注視点(ピボット位置)
	Vector3 pivot_ = { 0.0f,0.0f,0.0f };

	// ローカル座標
	Vector3 translation_ = { 0.0f,0.0f,-50.0f };

	// 射影行列
	Matrix4x4 projectionMatrix_;

	// ビュー行列
	Matrix4x4 viewMatrix_;

public:

	/// <summary>
	/// 初期化
	/// </summary>
	void Initialize();

	/// <summary>
	/// 更新処理
	/// </summary>
	void Update(BYTE* keys);

	// ゲッターロボ
	Matrix4x4 GetViewMatrix();

};

