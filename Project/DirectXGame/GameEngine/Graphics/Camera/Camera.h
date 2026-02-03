#pragma once
#include"Transform.h"
#include"Matrix4x4.h"
#include"MathUtility.h"
#include"WindowsApplication.h"

class Camera
{
	Transform transform_;
	Matrix4x4 worldMatrix_;
	Matrix4x4 viewMatrix_;

	Matrix4x4 projectionMatrix_;
	float horizontalFovY_;   // 水平方向視野角
	float aspectRatio_;     // アスペクト比
	float nearClip_;        // ニアクリップ距離
	float farClip_;         // ファークリップ距離

	Matrix4x4 viewProjectionMatrix_;

public:

	// デフォルトコンストラクタ
	Camera();

	// 更新
	void Update();

	/// <summary>
    /// ImGuiでカメラのパラメータを調整する
    /// </summary>
	void OnImGui();

	// セッター
	void SetRotate(const Vector3& rotate) { transform_.rotate = rotate; }
	void SetTranslate(const Vector3& translate) { transform_.translate = translate; }
	void SetFovY(const float& fovY) { horizontalFovY_ = fovY; }
	void SetAspectRatio(const float& aspectRatio) { aspectRatio_ = aspectRatio; }
	void SetNearClip(const float& nearClip) { nearClip_ = nearClip; }
	void SetFarClip(const float& farClip) { farClip_ = farClip; }

	// ゲッター
	const Matrix4x4& GetWorldMatrix() const { return worldMatrix_; }
	const Matrix4x4& GetViewMatrix() const { return viewMatrix_; }
	const Matrix4x4& GetProjectionMatrix() const { return projectionMatrix_; }
	const Matrix4x4& GetViewProjectionMatrix() const { return viewProjectionMatrix_; }
	const Vector3& GetRotate() const { return transform_.rotate; }
	const Vector3& GetTranslate() const { return transform_.translate; }

};

