#include "RailAimController.h"

#include "Camera.h"

#include <algorithm>
#include <cmath>

namespace {
	// 真上/真下の反転を防ぐ pitch の上限（±89°）
	constexpr float kPitchLimit = 1.5533431f; // 89 * pi / 180
}

void RailAimController::AddYawPitch(float dYaw, float dPitch) {
	euler_.y += dYaw * yawPitchSens_;
	euler_.x += dPitch * yawPitchSens_;
	euler_.x = std::clamp(euler_.x, -kPitchLimit, kPitchLimit);
}

void RailAimController::AddRoll(float dRoll) {
	euler_.z += dRoll * rollSens_;
}

void RailAimController::Apply(Camera* camera, const Vector3& eye) const {
	if (!camera) return;
	camera->SetTranslate(eye);
	camera->SetRotate(euler_);
}
