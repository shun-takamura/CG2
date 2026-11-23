#include "Camera.h"
#include "imgui.h"
#include "Easing.h"
#include <random>
namespace {
	// 初期位置で原点から手前（-Z）へ下がる距離 [m]
	constexpr float kDefaultBackDistance = 10.0f;
	// 初期の縦の画角 [rad]（約 25.8°）
	constexpr float kDefaultFovY = 0.45f;
	// 初期のニアクリップ・ファークリップ [m]
	constexpr float kDefaultNearClip = 0.1f;
	constexpr float kDefaultFarClip = 100.0f;
	// シェイクの乱数の種（毎回同じ揺れ方にする）
	constexpr unsigned int kShakeRandomSeed = 12345;
}

Camera::Camera()
	: transform_({ {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -kDefaultBackDistance} })
	, horizontalFovY_(kDefaultFovY)
	, aspectRatio_(float(WindowsApplication::kUIWidth) / float(WindowsApplication::kUIHeight))
	, nearClip_(kDefaultNearClip)
	, farClip_(kDefaultFarClip)
	, worldMatrix_(MakeAffineMatrix(transform_))
	, viewMatrix_(Inverse(worldMatrix_))
	, projectionMatrix_(MakePerspectiveFovMatrix(horizontalFovY_, aspectRatio_, nearClip_, farClip_))
	, viewProjectionMatrix_(Multiply(viewMatrix_, projectionMatrix_))
{
}

void Camera::OnImGui()
{
#ifdef USE_IMGUI
    ImGui::DragFloat3("Position", &transform_.translate.x, 0.1f);

    // 回転をDegreeで表示（内部はラジアン保存）
    Vector3 rotateDegree = RadToDeg(transform_.rotate);
    if (ImGui::DragFloat3("Rotation", &rotateDegree.x, 1.0f)) {
        transform_.rotate = DegToRad(rotateDegree);
    }

    ImGui::DragFloat3("Scale", &transform_.scale.x, 0.01f);
    ImGui::DragFloat("FovY", &horizontalFovY_, 0.01f, 0.01f, 3.14f);
    ImGui::DragFloat("NearClip", &nearClip_, 0.01f, 0.001f, 10.0f);
    ImGui::DragFloat("FarClip", &farClip_, 25.0f, 1.0f, 50000.0f);
#endif
}

void Camera::UpdateShake(float deltaTime)
{
	if (shakeIntensity_ <= 0.0f) {
		shakeOffset_ = { 0.0f, 0.0f, 0.0f };
		return;
	}

	shakeElapsed_ += deltaTime;

	if (shakeElapsed_ < shakeDuration_) {
		// EaseOut で減衰：最初は強く、徐々に弱く（1 - EaseOutCubic(t)）
		float t = shakeElapsed_ / shakeDuration_;
		float damping = 1.0f - Easing::EaseOutCubic(t);

		static std::mt19937 gen(kShakeRandomSeed);
		std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
		shakeOffset_ = {
			dist(gen) * shakeIntensity_ * damping,
			dist(gen) * shakeIntensity_ * damping,
			0.0f // Z 方向は揺らさない（FOV 感が変わって見えるため）
		};
	} else {
		shakeIntensity_ = 0.0f;
		shakeOffset_ = { 0.0f, 0.0f, 0.0f };
	}
}

void Camera::Update()
{
	// シェイクオフセット適用済みの transform を作る
	Transform shakingTransform = transform_;
	shakingTransform.translate.x += shakeOffset_.x;
	shakingTransform.translate.y += shakeOffset_.y;
	shakingTransform.translate.z += shakeOffset_.z;

	worldMatrix_ = MakeAffineMatrix(shakingTransform);
	viewMatrix_ = Inverse(worldMatrix_);

	projectionMatrix_ = MakePerspectiveFovMatrix(horizontalFovY_, aspectRatio_, nearClip_, farClip_);

	viewProjectionMatrix_ = Multiply(viewMatrix_, projectionMatrix_);
}
