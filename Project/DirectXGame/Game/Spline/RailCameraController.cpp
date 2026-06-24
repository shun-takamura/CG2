#include "RailCameraController.h"

#include "Camera.h"
#include "SplineCurveActor.h"
#include "CameraRotKey.h"
#include "Quaternion.h"

#include <algorithm>
#include <cmath>

namespace {
	Vector3 SafeNormalize(const Vector3& v) {
		float len = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
		if (len < 1e-6f) return { 0.0f, 0.0f, 1.0f };
		return { v.x / len, v.y / len, v.z / len };
	}

	// オイラー角→クオータニオン。カメラの SetRotate と EulerFromQuaternion が使う
	// MakeRotateMatrix(Vector3)=Rz*Ry*Rx 規約に一致させる。
	// MakeRotateMatrix(Multiply(a,b)) = M(b)*M(a) の逆順合成になるため、
	// Rz*Ry*Rx を得るには qx→qy→qz の順で Multiply する（engine の QuaternionFromEuler は
	// 逆順で Rx*Ry*Rz になり、ここでは使えない）。
	Quaternion EulerToQuatZYX(const Vector3& e) {
		Quaternion qx = MakeRotateAxisAngleQuaternion({ 1.0f, 0.0f, 0.0f }, e.x);
		Quaternion qy = MakeRotateAxisAngleQuaternion({ 0.0f, 1.0f, 0.0f }, e.y);
		Quaternion qz = MakeRotateAxisAngleQuaternion({ 0.0f, 0.0f, 1.0f }, e.z);
		return Multiply(Multiply(qx, qy), qz);
	}
}

void RailCameraController::Initialize(Camera* camera) {
	camera_ = camera;
}

void RailCameraController::Update(float deltaTime) {
	if (!camera_ || !cameraPath_) return;

	if (!paused_) {
		progress_ += speed_ * deltaTime;
		if (loop_) {
			progress_ -= std::floor(progress_);
		} else {
			progress_ = std::clamp(progress_, 0.0f, 1.0f);
		}
	}

	const Vector3 eye = cameraPath_->Sample(progress_);

	// ----- 向き：回転キー列を評価（無ければ接線方向を向く保険）-----
	Vector3 euler{ 0.0f, 0.0f, 0.0f };
	const size_t n = rotKeys_ ? rotKeys_->size() : 0;

	if (n == 0) {
		// スプライン接線方向を向く（roll=0）。オーサリング前でも前を向くための保険。
		const float eps = 0.01f;
		const Vector3 p0 = cameraPath_->Sample((std::min)(progress_, 1.0f - eps));
		const Vector3 p1 = cameraPath_->Sample((std::min)(progress_ + eps, 1.0f));
		const Vector3 fwd = SafeNormalize({ p1.x - p0.x, p1.y - p0.y, p1.z - p0.z });
		euler = { -std::asin(std::clamp(fwd.y, -1.0f, 1.0f)),
				  std::atan2(fwd.x, fwd.z),
				  0.0f };
	} else if (n == 1) {
		euler = (*rotKeys_)[0]->rotate;
	} else {
		const auto& keys = *rotKeys_;
		if (progress_ <= keys[0]->t) {
			euler = keys[0]->rotate;
		} else if (progress_ >= keys[n - 1]->t) {
			euler = keys[n - 1]->rotate;
		} else {
			// progress_ を挟む区間 [A,B] を探す（t 昇順前提）
			size_t i = 0;
			for (; i + 1 < n; ++i) {
				if (progress_ < keys[i + 1]->t) break;
			}
			const CameraRotKey* A = keys[i].get();
			const CameraRotKey* B = keys[i + 1].get();
			const float denom = B->t - A->t;
			float u = denom > 1e-6f ? (progress_ - A->t) / denom : 0.0f;
			u = std::clamp(u, 0.0f, 1.0f);
			const float uu = A->easeToNext.Evaluate(u);
			const Quaternion q = Slerp(EulerToQuatZYX(A->rotate),
									   EulerToQuatZYX(B->rotate), uu);
			euler = EulerFromQuaternion(q);
		}
	}

	camera_->SetTranslate(eye);
	camera_->SetRotate(euler);
}
