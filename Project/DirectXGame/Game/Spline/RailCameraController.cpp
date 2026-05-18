#include "RailCameraController.h"

#include "Camera.h"
#include "SplineCurveActor.h"

#include <algorithm>
#include <cmath>

namespace {
	Vector3 SafeNormalize(const Vector3& v) {
		float len = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
		if (len < 1e-6f) return { 0.0f, 0.0f, 1.0f };
		return { v.x / len, v.y / len, v.z / len };
	}
}

void RailCameraController::Initialize(Camera* camera) {
	camera_ = camera;
}

void RailCameraController::Update(float deltaTime) {
	if (!camera_ || !cameraPath_ || !lookAtPath_) return;

	if (!paused_) {
		progress_ += speed_ * deltaTime;
		if (loop_) {
			progress_ -= std::floor(progress_);
		} else {
			progress_ = std::clamp(progress_, 0.0f, 1.0f);
		}
	}

	const Vector3 eye    = cameraPath_->Sample(progress_);
	const Vector3 target = lookAtPath_->Sample(progress_);

	const Vector3 forward = SafeNormalize({ target.x - eye.x, target.y - eye.y, target.z - eye.z });

	// 進行方向ベクトルからオイラー角を逆算（roll=0、Yaw→Pitch の順で復元）
	// 回転順は MakeRotateMatrix が Z*Y*X（XYZ 適用順）。
	// 標準的な向き計算: yaw=atan2(fx, fz), pitch=-asin(fy)
	const float yaw   = std::atan2(forward.x, forward.z);
	const float pitch = -std::asin(std::clamp(forward.y, -1.0f, 1.0f));

	camera_->SetTranslate(eye);
	camera_->SetRotate({ pitch, yaw, 0.0f });
}
