#include "CloudTopIntro.h"

#include "Camera.h"
#include "MathUtility.h"
#include "Transform.h"
#include "Json/JsonValue.h"
#include <algorithm>
#include <cmath>

#ifdef _DEBUG
#include "imgui.h"
#endif

namespace {
	float SmoothStep01(float u) {
		u = std::clamp(u, 0.0f, 1.0f);
		return u * u * (3.0f - 2.0f * u);
	}

	// 速さ 0 から入り速さ 0 で抜ける（smoothstep より出だしと終わりがゆるやか）
	float SmootherStep01(float u) {
		u = std::clamp(u, 0.0f, 1.0f);
		return u * u * u * (u * (u * 6.0f - 15.0f) + 10.0f);
	}

	// a → b の差を −π..π に
	float WrapAngle(float a) {
		while (a > kPi) a -= 2.0f * kPi;
		while (a < -kPi) a += 2.0f * kPi;
		return a;
	}

	Vector3 Add(const Vector3& a, const Vector3& b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
	Vector3 Sub(const Vector3& a, const Vector3& b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
	Vector3 Scale(const Vector3& v, float s) { return { v.x * s, v.y * s, v.z * s }; }
}

void CloudTopIntro::Begin() {
	active_ = true;
	applied_ = false;
	elapsed_ = 0.0f;
}

float CloudTopIntro::GetTotalSeconds() const {
	return (std::max)(holdSeconds_, 0.0f) + (std::max)(orbitSeconds_, 0.0f);
}

void CloudTopIntro::Apply(Camera* camera, const Vector3& playerWorldPos, float deltaTime) {
	applied_ = false;
	if (!active_ || !camera) return;
	elapsed_ += deltaTime;
	if (elapsed_ >= GetTotalSeconds()) {
		// 合流した：このフレームからレールのカメラのまま
		active_ = false;
		return;
	}

	railEye_ = camera->GetTranslate();
	railRotate_ = camera->GetRotate();
	railFovY_ = camera->GetFovY();

	// レールのカメラの向きの空間（右・上・前）。カメラの行列と同じ Rx·Ry·Rz（回転順の罠）
	const Matrix4x4 basis = MakeAffineMatrix(Transform{ { 1.0f, 1.0f, 1.0f }, railRotate_, { 0.0f, 0.0f, 0.0f } });
	const Vector3 right = { basis.m[0][0], basis.m[0][1], basis.m[0][2] };
	const Vector3 up = { basis.m[1][0], basis.m[1][1], basis.m[1][2] };
	const Vector3 forward = { basis.m[2][0], basis.m[2][1], basis.m[2][2] };
	const Vector3 focus = Add(playerWorldPos, Scale(up, focusHeight_));

	// 終わり＝レールのカメラを注視点から見た方位・仰角・距離
	const Vector3 d = Sub(railEye_, focus);
	const float endX = Dot(d, right), endY = Dot(d, up), endZ = Dot(d, forward);
	const float endDistance = (std::max)(Length(d), 1e-3f);
	const float endElevation = std::asin(std::clamp(endY / endDistance, -1.0f, 1.0f));
	float endAzimuth = std::atan2(endX, endZ);
	const float startAzimuth = DegToRad(startAzimuthDeg_);
	// 回る向き：右側を回るなら方位を増やして（前 → 右 → 後ろ）、左側なら減らして合流する
	if (orbitRight_) {
		while (endAzimuth < startAzimuth) endAzimuth += 2.0f * kPi;
		while (endAzimuth - startAzimuth > 2.0f * kPi) endAzimuth -= 2.0f * kPi;
	} else {
		while (endAzimuth > startAzimuth) endAzimuth -= 2.0f * kPi;
		while (startAzimuth - endAzimuth > 2.0f * kPi) endAzimuth += 2.0f * kPi;
	}

	const float u = orbitSeconds_ > 1e-3f ? std::clamp((elapsed_ - holdSeconds_) / orbitSeconds_, 0.0f, 1.0f) : 1.0f;
	const float e = SmootherStep01(u);
	const float azimuth = startAzimuth + (endAzimuth - startAzimuth) * e;
	const float elevation = DegToRad(startElevationDeg_) + (endElevation - DegToRad(startElevationDeg_)) * e;
	// 距離は比で補間（近い所から遠ざかるときに急に離れて見えないように）
	const float startDistance = (std::max)(startDistance_, 1e-3f);
	const float distance = startDistance * std::pow(endDistance / startDistance, e);

	const float ce = std::cos(elevation);
	const Vector3 local = { distance * ce * std::sin(azimuth), distance * std::sin(elevation), distance * ce * std::cos(azimuth) };
	introEye_ = Add(focus, Add(Scale(right, local.x), Add(Scale(up, local.y), Scale(forward, local.z))));

	// 向き：注視点を見る → 回り込みの後半でレールのカメラの向きへ（終わりで一致する）
	const Vector3 look = Sub(focus, introEye_);
	const float lookYaw = std::atan2(look.x, look.z);
	const float lookPitch = -std::atan2(look.y, std::sqrt(look.x * look.x + look.z * look.z));
	const float blendSpan = (std::max)(1.0f - lookBlendStart_, 1e-3f);
	const float w = SmoothStep01((u - lookBlendStart_) / blendSpan);
	introRotate_ = {
		lookPitch + WrapAngle(railRotate_.x - lookPitch) * w,
		lookYaw + WrapAngle(railRotate_.y - lookYaw) * w,
		railRotate_.z * w,
	};
	introFovY_ = DegToRad(startFovDeg_) + (railFovY_ - DegToRad(startFovDeg_)) * e;

	applied_ = true;
	UseIntroPose(camera);
}

void CloudTopIntro::UseRailPose(Camera* camera) const {
	if (!active_ || !applied_ || !camera) return;
	camera->SetTranslate(railEye_);
	camera->SetRotate(railRotate_);
	camera->SetFovY(railFovY_);
	camera->Update();
}

void CloudTopIntro::UseIntroPose(Camera* camera) const {
	if (!active_ || !applied_ || !camera) return;
	camera->SetTranslate(introEye_);
	camera->SetRotate(introRotate_);
	camera->SetFovY(introFovY_);
	camera->Update();
}

void CloudTopIntro::LoadTuning(const JsonValue& node) {
	if (!node.IsObject()) return;
	auto read = [&node](const char* key, float& value) {
		value = static_cast<float>(node[key].AsDouble(value));
	};
	read("holdSeconds", holdSeconds_);
	read("orbitSeconds", orbitSeconds_);
	read("startAzimuthDeg", startAzimuthDeg_);
	read("startElevationDeg", startElevationDeg_);
	read("startDistance", startDistance_);
	read("startFovDeg", startFovDeg_);
	orbitRight_ = node["orbitRight"].AsBool(orbitRight_);
	read("focusHeight", focusHeight_);
	read("lookBlendStart", lookBlendStart_);
	read("wingDeployDelay", wingDeployDelay_);
}

void CloudTopIntro::SaveTuning(JsonValue& node) const {
	node = JsonValue::MakeObject();
	auto write = [&node](const char* key, float value) {
		node[key] = JsonValue(static_cast<double>(value));
	};
	write("holdSeconds", holdSeconds_);
	write("orbitSeconds", orbitSeconds_);
	write("startAzimuthDeg", startAzimuthDeg_);
	write("startElevationDeg", startElevationDeg_);
	write("startDistance", startDistance_);
	write("startFovDeg", startFovDeg_);
	node["orbitRight"] = JsonValue(orbitRight_);
	write("focusHeight", focusHeight_);
	write("lookBlendStart", lookBlendStart_);
	write("wingDeployDelay", wingDeployDelay_);
}

void CloudTopIntro::OnImGui() {
#ifdef _DEBUG
	ImGui::Text("active = %s  elapsed %.2f / %.2f s", active_ ? "yes" : "no", elapsed_, GetTotalSeconds());
	ImGui::DragFloat("Hold (s)", &holdSeconds_, 0.01f, 0.0f, 5.0f);
	ImGui::DragFloat("Orbit (s)", &orbitSeconds_, 0.01f, 0.0f, 10.0f);
	ImGui::DragFloat("Start Azimuth (deg, +right)", &startAzimuthDeg_, 0.5f, -180.0f, 180.0f);
	ImGui::DragFloat("Start Elevation (deg)", &startElevationDeg_, 0.5f, -80.0f, 80.0f);
	ImGui::DragFloat("Start Distance (m)", &startDistance_, 0.05f, 0.5f, 100.0f);
	ImGui::DragFloat("Start FovY (deg)", &startFovDeg_, 0.1f, 5.0f, 120.0f);
	ImGui::Checkbox("Orbit Around Right Side", &orbitRight_);
	ImGui::DragFloat("Focus Height (m)", &focusHeight_, 0.01f, -5.0f, 5.0f);
	ImGui::SliderFloat("Look Blend Start", &lookBlendStart_, 0.0f, 1.0f);
	ImGui::DragFloat("Wing Deploy Delay (s)", &wingDeployDelay_, 0.01f, 0.0f, 5.0f);
#endif
}
