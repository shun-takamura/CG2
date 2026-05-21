#include "Camera.h"
#include "imgui.h"
#include "Easing.h"
#include <random>

Camera::Camera()
	: transform_({ {1.0f, 1.0f, 1.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -10.0f} })
	, horizontalFovY_(0.45f)
	, aspectRatio_(float(WindowsApplication::kClientWidth) / float(WindowsApplication::kClientHeight))
	, nearClip_(0.1f)
	, farClip_(100.0f)
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

    Vector3 rotateDegree = {
        transform_.rotate.x * (180.0f / 3.14159265f),
        transform_.rotate.y * (180.0f / 3.14159265f),
        transform_.rotate.z * (180.0f / 3.14159265f)
    };
    if (ImGui::DragFloat3("Rotation", &rotateDegree.x, 1.0f)) {
        transform_.rotate.x = rotateDegree.x * (3.14159265f / 180.0f);
        transform_.rotate.y = rotateDegree.y * (3.14159265f / 180.0f);
        transform_.rotate.z = rotateDegree.z * (3.14159265f / 180.0f);
    }

    ImGui::DragFloat3("Scale", &transform_.scale.x, 0.01f);
    ImGui::DragFloat("FovY", &horizontalFovY_, 0.01f, 0.01f, 3.14f);
    ImGui::DragFloat("NearClip", &nearClip_, 0.01f, 0.001f, 10.0f);
    ImGui::DragFloat("FarClip", &farClip_, 1.0f, 1.0f, 1000.0f);
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

		static std::mt19937 gen(12345);
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
