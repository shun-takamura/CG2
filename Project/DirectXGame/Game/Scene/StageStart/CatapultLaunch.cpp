#include "CatapultLaunch.h"

#include "Json/JsonValue.h"
#include <algorithm>
#include <cmath>

#ifdef _DEBUG
#include "imgui.h"
#endif

namespace {
	const char* PhaseName(CatapultLaunch::Phase phase) {
		switch (phase) {
		case CatapultLaunch::Phase::Idle:      return "Idle";
		case CatapultLaunch::Phase::Charging:  return "Charging";
		case CatapultLaunch::Phase::Charged:   return "Charged";
		case CatapultLaunch::Phase::Launching: return "Launching";
		}
		return "";
	}
}

void CatapultLaunch::Reset() {
	phase_ = Phase::Idle;
	phaseTime_ = 0.0f;
	thrust_ = 0.0f;
	thrustStarted_ = false;
}

void CatapultLaunch::BeginCharge() {
	if (phase_ != Phase::Idle) return;
	phase_ = Phase::Charging;
	phaseTime_ = 0.0f;
}

void CatapultLaunch::BeginLaunch() {
	phase_ = Phase::Launching;
	phaseTime_ = 0.0f;
	thrust_ = 1.0f;
	thrustStarted_ = true;
}

void CatapultLaunch::Update(float deltaTime, bool thrustHeld) {
	time_ += deltaTime;
	phaseTime_ += deltaTime;
	switch (phase_) {
	case Phase::Idle:
		break;
	case Phase::Charging:
		if (phaseTime_ >= chargeSeconds_) {
			phase_ = Phase::Charged;
			phaseTime_ = 0.0f;
		}
		break;
	case Phase::Charged:
		// 押している間は上がり、離すと下がる
		if (thrustHeld) {
			thrust_ += deltaTime / (std::max)(thrustRiseSeconds_, 1e-3f);
			thrustStarted_ = true;
		} else {
			thrust_ -= deltaTime / (std::max)(thrustFallSeconds_, 1e-3f);
		}
		thrust_ = std::clamp(thrust_, 0.0f, 1.0f);
		break;
	case Phase::Launching:
		thrust_ = 1.0f;
		break;
	}
}

float CatapultLaunch::GetRpm() const {
	// 上の方で伸びが鈍る（回転数らしい）。回っている間は少し揺らす
	const float eased = 1.0f - std::pow(1.0f - std::clamp(thrust_, 0.0f, 1.0f), (std::max)(thrustRiseCurve_, 0.1f));
	float rpm = idleRpm_ + (maxRpm_ - idleRpm_) * eased;
	if (thrust_ > 0.0f) rpm += rpmJitter_ * (std::sin(time_ * 37.0f) * 0.6f + std::sin(time_ * 91.0f) * 0.4f) * thrust_;
	return (std::max)(rpm, 0.0f);
}

float CatapultLaunch::GetTravel() const {
	if (phase_ != Phase::Launching) return 0.0f;
	const float T = (std::max)(launchSeconds_, 1e-3f);
	const float p = (std::max)(launchExponent_, 1.0f);
	if (phaseTime_ <= T) return exitTravel_ * std::pow(phaseTime_ / T, p);
	// 射出口を過ぎたら最後の速さのまま
	return exitTravel_ + GetSpeed() * (phaseTime_ - T);
}

float CatapultLaunch::GetSpeed() const {
	if (phase_ != Phase::Launching) return 0.0f;
	const float T = (std::max)(launchSeconds_, 1e-3f);
	const float p = (std::max)(launchExponent_, 1.0f);
	const float t = (std::min)(phaseTime_, T);
	return exitTravel_ * p * std::pow(t / T, p - 1.0f) / T;
}

float CatapultLaunch::GetMaxSpeed() const {
	const float T = (std::max)(launchSeconds_, 1e-3f);
	return exitTravel_ * (std::max)(launchExponent_, 1.0f) / T;
}

void CatapultLaunch::LoadTuning(const JsonValue& node) {
	if (!node.IsObject()) return;
	auto read = [&node](const char* key, float& value) {
		value = static_cast<float>(node[key].AsDouble(value));
	};
	read("chargeSeconds", chargeSeconds_);
	read("thrustRiseSeconds", thrustRiseSeconds_);
	read("thrustFallSeconds", thrustFallSeconds_);
	read("thrustRiseCurve", thrustRiseCurve_);
	read("idleRpm", idleRpm_);
	read("maxRpm", maxRpm_);
	read("redZone", redZone_);
	read("rpmJitter", rpmJitter_);
	read("launchSeconds", launchSeconds_);
	read("launchExponent", launchExponent_);
	read("cablePurgeRatio", cablePurgeRatio_);
	read("launchShakeIntensity", launchShakeIntensity_);
	read("launchShakeSeconds", launchShakeSeconds_);
}

void CatapultLaunch::SaveTuning(JsonValue& node) const {
	node = JsonValue::MakeObject();
	auto write = [&node](const char* key, float value) {
		node[key] = JsonValue(static_cast<double>(value));
	};
	write("chargeSeconds", chargeSeconds_);
	write("thrustRiseSeconds", thrustRiseSeconds_);
	write("thrustFallSeconds", thrustFallSeconds_);
	write("thrustRiseCurve", thrustRiseCurve_);
	write("idleRpm", idleRpm_);
	write("maxRpm", maxRpm_);
	write("redZone", redZone_);
	write("rpmJitter", rpmJitter_);
	write("launchSeconds", launchSeconds_);
	write("launchExponent", launchExponent_);
	write("cablePurgeRatio", cablePurgeRatio_);
	write("launchShakeIntensity", launchShakeIntensity_);
	write("launchShakeSeconds", launchShakeSeconds_);
}

void CatapultLaunch::OnImGui() {
#ifdef _DEBUG
	ImGui::Text("phase %s (%.2f s)  thrust %.2f  rpm %.0f  travel %.1f m  speed %.1f m/s", PhaseName(phase_), phaseTime_,
		thrust_, GetRpm(), GetTravel(), GetSpeed());
	ImGui::DragFloat("Charge (s)", &chargeSeconds_, 0.01f, 0.0f, 10.0f);
	ImGui::DragFloat("Thrust Rise (s)", &thrustRiseSeconds_, 0.01f, 0.05f, 10.0f);
	ImGui::DragFloat("Thrust Fall (s)", &thrustFallSeconds_, 0.01f, 0.05f, 10.0f);
	ImGui::DragFloat("Thrust Rise Curve", &thrustRiseCurve_, 0.01f, 0.1f, 5.0f);
	ImGui::DragFloat("Idle RPM", &idleRpm_, 10.0f, 0.0f, 20000.0f);
	ImGui::DragFloat("Max RPM", &maxRpm_, 10.0f, 100.0f, 40000.0f);
	ImGui::SliderFloat("Red Zone", &redZone_, 0.0f, 1.0f);
	ImGui::DragFloat("RPM Jitter", &rpmJitter_, 1.0f, 0.0f, 2000.0f);
	ImGui::DragFloat("Launch (s)", &launchSeconds_, 0.01f, 0.2f, 10.0f);
	ImGui::DragFloat("Launch Exponent", &launchExponent_, 0.01f, 1.0f, 5.0f);
	ImGui::SliderFloat("Cable Purge Ratio", &cablePurgeRatio_, 0.0f, 1.0f);
	ImGui::DragFloat("Launch Shake", &launchShakeIntensity_, 0.005f, 0.0f, 1.0f);
	ImGui::DragFloat("Launch Shake (s)", &launchShakeSeconds_, 0.01f, 0.0f, 3.0f);
#endif // _DEBUG
}
