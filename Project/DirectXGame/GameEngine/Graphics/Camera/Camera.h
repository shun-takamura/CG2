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

	// カメラシェイク
	float shakeIntensity_ = 0.0f;        // シェイク強度
	float shakeDuration_ = 0.0f;         // シェイク継続時間
	float shakeElapsed_ = 0.0f;          // シェイク経過時間
	Vector3 shakeOffset_{ 0.0f, 0.0f, 0.0f }; // 適用中のオフセット（Update() で加算される）

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

	/// <summary>
	/// 外部（DebugCamera等）から計算済みの行列を直接注入する。
	/// 通常の Update() を経由せず、view/projection を上書きしたい場合に使う。
	/// </summary>
	void SetExternalMatrices(const Matrix4x4& view, const Matrix4x4& projection, const Vector3& translate) {
		viewMatrix_ = view;
		projectionMatrix_ = projection;
		viewProjectionMatrix_ = Multiply(view, projection);
		transform_.translate = translate;
	}
	void SetFovY(const float& fovY) { horizontalFovY_ = fovY; }
	void SetAspectRatio(const float& aspectRatio) { aspectRatio_ = aspectRatio; }
	void SetNearClip(const float& nearClip) { nearClip_ = nearClip; }
	void SetFarClip(const float& farClip) { farClip_ = farClip; }

	/// <summary>
	/// カメラシェイクを開始する（intensity: 強度, duration: 継続時間[秒]）
	/// </summary>
	void Shake(float intensity, float duration) {
		shakeIntensity_ = intensity;
		shakeDuration_ = duration;
		shakeElapsed_ = 0.0f;
	}

	/// <summary>
	/// シェイクを即座に停止しオフセットを消す（World 停止中などに UpdateShake が進まず
	/// 揺れが固まるのを防ぐため、必殺技発動時などに明示的に呼ぶ）。
	/// </summary>
	void StopShake() {
		shakeIntensity_ = 0.0f;
		shakeDuration_ = 0.0f;
		shakeElapsed_ = 0.0f;
		shakeOffset_ = { 0.0f, 0.0f, 0.0f };
	}

	/// <summary>
	/// シェイクのオフセット計算を deltaTime 進める。
	/// シーン側から Update() より前に呼ぶ。
	/// </summary>
	void UpdateShake(float deltaTime);

	// ゲッター
	const Matrix4x4& GetWorldMatrix() const { return worldMatrix_; }
	const Matrix4x4& GetViewMatrix() const { return viewMatrix_; }
	const Matrix4x4& GetProjectionMatrix() const { return projectionMatrix_; }
	const Matrix4x4& GetViewProjectionMatrix() const { return viewProjectionMatrix_; }
	const Vector3& GetRotate() const { return transform_.rotate; }
	const Vector3& GetTranslate() const { return transform_.translate; }
	Vector3 GetForward() const { return Normalize(Vector3{ worldMatrix_.m[2][0], worldMatrix_.m[2][1], worldMatrix_.m[2][2] }); }
	Vector3 GetUp()      const { return Normalize(Vector3{ worldMatrix_.m[1][0], worldMatrix_.m[1][1], worldMatrix_.m[1][2] }); }
	float GetFovY() const         { return horizontalFovY_; }
	float GetAspectRatio() const  { return aspectRatio_; }
	float GetNearClip() const     { return nearClip_; }
	float GetFarClip() const      { return farClip_; }
};

