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

	/// <summary>位置 (0,0,-10)・回転 0・視野角 0.45 rad・UI 解像度の縦横比・near 0.1・far 100 で行列を作るコンストラクタ</summary>
	Camera();

	/// <summary>
	/// シェイクのオフセットを足した Transform からワールド・ビュー行列を作り、
	/// 射影行列とビュー射影行列を作り直す
	/// </summary>
	void Update();

	/// <summary>
	/// ImGuiでカメラのパラメータ（位置・回転[度]・スケール・視野角・near/far）を調整する
	/// </summary>
	void OnImGui();

	// セッター
	/// <summary>カメラの回転を設定する</summary>
	/// <param name="rotate">オイラー角 [ラジアン]</param>
	void SetRotate(const Vector3& rotate) { transform_.rotate = rotate; }
	/// <summary>カメラの位置を設定する</summary>
	/// <param name="translate">ワールド座標 [m]</param>
	void SetTranslate(const Vector3& translate) { transform_.translate = translate; }

	/// <summary>
	/// 外部（DebugCamera等）から計算済みの行列を直接注入する。
	/// 通常の Update() を経由せず、view/projection を上書きしたい場合に使う。
	/// </summary>
	/// <param name="view">ビュー行列</param>
	/// <param name="projection">射影行列</param>
	/// <param name="translate">カメラの位置（ワールド座標 [m]）</param>
	void SetExternalMatrices(const Matrix4x4& view, const Matrix4x4& projection, const Vector3& translate) {
		viewMatrix_ = view;
		projectionMatrix_ = projection;
		viewProjectionMatrix_ = Multiply(view, projection);
		transform_.translate = translate;
	}
	/// <summary>視野角を設定する（次の Update で射影行列に反映）</summary>
	/// <param name="fovY">視野角 [ラジアン]</param>
	void SetFovY(const float& fovY) { horizontalFovY_ = fovY; }
	/// <summary>縦横比を設定する（次の Update で射影行列に反映）</summary>
	/// <param name="aspectRatio">幅 / 高さ</param>
	void SetAspectRatio(const float& aspectRatio) { aspectRatio_ = aspectRatio; }
	/// <summary>ニアクリップ距離を設定する</summary>
	/// <param name="nearClip">距離 [m]</param>
	void SetNearClip(const float& nearClip) { nearClip_ = nearClip; }
	/// <summary>ファークリップ距離を設定する</summary>
	/// <param name="farClip">距離 [m]</param>
	void SetFarClip(const float& farClip) { farClip_ = farClip; }

	/// <summary>
	/// カメラシェイクを開始する（揺れは EaseOutCubic で減衰し、Z 方向には揺らさない）
	/// </summary>
	/// <param name="intensity">揺れの最大幅 [m]</param>
	/// <param name="duration">継続時間 [秒]</param>
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
	/// <param name="deltaTime">経過時間 [秒]</param>
	void UpdateShake(float deltaTime);

	// ゲッター
	/// <summary>ワールド行列（シェイク込み）を返す</summary>
	/// <returns>ワールド行列</returns>
	const Matrix4x4& GetWorldMatrix() const { return worldMatrix_; }
	/// <summary>ビュー行列を返す</summary>
	/// <returns>ビュー行列</returns>
	const Matrix4x4& GetViewMatrix() const { return viewMatrix_; }
	/// <summary>射影行列を返す</summary>
	/// <returns>射影行列</returns>
	const Matrix4x4& GetProjectionMatrix() const { return projectionMatrix_; }
	/// <summary>ビュー射影行列を返す</summary>
	/// <returns>ビュー × 射影の行列</returns>
	const Matrix4x4& GetViewProjectionMatrix() const { return viewProjectionMatrix_; }
	/// <summary>カメラの回転を返す</summary>
	/// <returns>オイラー角 [ラジアン]</returns>
	const Vector3& GetRotate() const { return transform_.rotate; }
	/// <summary>カメラの位置を返す（シェイクのオフセットは含まない）</summary>
	/// <returns>ワールド座標 [m]</returns>
	const Vector3& GetTranslate() const { return transform_.translate; }
	/// <summary>ワールド行列の 3 行目から視線の前方向を返す</summary>
	/// <returns>正規化した前方向ベクトル</returns>
	Vector3 GetForward() const { return Normalize(Vector3{ worldMatrix_.m[2][0], worldMatrix_.m[2][1], worldMatrix_.m[2][2] }); }
	/// <summary>ワールド行列の 2 行目からカメラの上方向を返す</summary>
	/// <returns>正規化した上方向ベクトル</returns>
	Vector3 GetUp()      const { return Normalize(Vector3{ worldMatrix_.m[1][0], worldMatrix_.m[1][1], worldMatrix_.m[1][2] }); }
	/// <summary>視野角を返す</summary>
	/// <returns>視野角 [ラジアン]</returns>
	float GetFovY() const         { return horizontalFovY_; }
	/// <summary>縦横比を返す</summary>
	/// <returns>幅 / 高さ</returns>
	float GetAspectRatio() const  { return aspectRatio_; }
	/// <summary>ニアクリップ距離を返す</summary>
	/// <returns>距離 [m]</returns>
	float GetNearClip() const     { return nearClip_; }
	/// <summary>ファークリップ距離を返す</summary>
	/// <returns>距離 [m]</returns>
	float GetFarClip() const      { return farClip_; }
};

