#include "LaunchChecklist.h"

#include "InputManager.h"
#include "InputAction.h"
#include "KeyboardInput.h"
#include "MouseInput.h"
#include "ControllerInput.h"
#include "Config/GameActions.h"
#include "Json/JsonValue.h"
#include <algorithm>
#include <cmath>

#ifdef _DEBUG
#include "imgui.h"
#endif

void LaunchChecklist::Reset() {
	time_ = 0.0f;
	clearedTime_.fill(-1.0f);
	aimDistance_ = 0.0f;
	thrusterDone_.fill(false);
	thrusterHeld_.fill(false);
	weaponCount_ = 0;
}

void LaunchChecklist::Clear(Item item) {
	float& t = clearedTime_[static_cast<int>(item)];
	if (t < 0.0f) t = time_;
}

float LaunchChecklist::TimeSinceCleared(Item item) const {
	const float t = clearedTime_[static_cast<int>(item)];
	return t < 0.0f ? -1.0f : time_ - t;
}

int LaunchChecklist::ClearedCount() const {
	return static_cast<int>(std::count_if(clearedTime_.begin(), clearedTime_.end(), [](float t) { return t >= 0.0f; }));
}

float LaunchChecklist::GetAimProgress() const {
	return aimDistancePx_ > 1e-3f ? std::clamp(aimDistance_ / aimDistancePx_, 0.0f, 1.0f) : 1.0f;
}

void LaunchChecklist::Update(InputManager* input, float deltaTime, bool autoClear) {
	time_ += deltaTime;
	thrusterHeld_.fill(false);
	if (autoClear) {
		UpdateAuto(deltaTime);
		return;
	}
	if (!input) return;

	ControllerInput* pad = input->GetController();
	const bool padOn = pad && pad->IsConnected();

	// 照準：マウスの移動量（DirectInput の相対値）＋右スティック
	if (MouseInput* mouse = input->GetMouse()) {
		const float dx = static_cast<float>(mouse->GetDeltaX());
		const float dy = static_cast<float>(mouse->GetDeltaY());
		aimDistance_ += std::sqrt(dx * dx + dy * dy);
	}
	if (padOn) aimDistance_ += pad->GetRightStick().magnitude * aimStickSpeed_ * deltaTime;
	if (GetAimProgress() >= 1.0f) Clear(Item::Aim);

	// スラスター：W A S D（左スティックの 4 方向）
	if (KeyboardInput* kb = input->GetKeyboard()) {
		thrusterHeld_[kThrusterUp] = kb->PushKey(DIK_W);
		thrusterHeld_[kThrusterLeft] = kb->PushKey(DIK_A);
		thrusterHeld_[kThrusterDown] = kb->PushKey(DIK_S);
		thrusterHeld_[kThrusterRight] = kb->PushKey(DIK_D);
	}
	if (padOn) {
		const auto& ls = pad->GetLeftStick();
		const float x = ls.x * ls.magnitude;
		const float y = ls.y * ls.magnitude;
		thrusterHeld_[kThrusterUp] = thrusterHeld_[kThrusterUp] || y > stickThreshold_;
		thrusterHeld_[kThrusterDown] = thrusterHeld_[kThrusterDown] || y < -stickThreshold_;
		thrusterHeld_[kThrusterLeft] = thrusterHeld_[kThrusterLeft] || x < -stickThreshold_;
		thrusterHeld_[kThrusterRight] = thrusterHeld_[kThrusterRight] || x > stickThreshold_;
	}
	for (int i = 0; i < kThrusterDirCount; ++i) thrusterDone_[i] = thrusterDone_[i] || thrusterHeld_[i];
	if (std::all_of(thrusterDone_.begin(), thrusterDone_.end(), [](bool done) { return done; })) Clear(Item::Thruster);

	// 武装：射撃
	if (InputActionMap* actions = input->GetActionMap(); actions && actions->IsTriggered(static_cast<int>(Action::Fire))) {
		weaponCount_ = (std::min)(weaponCount_ + 1, weaponShots_);
	}
	if (weaponCount_ >= weaponShots_) Clear(Item::Weapon);
}

void LaunchChecklist::UpdateAuto(float deltaTime) {
	(void)deltaTime;
	// 項目 i は autoStartDelay + interval·i で CLEAR。その間の進み具合も見せる（照準のゲージ・4 方向・射撃の数）
	auto progress = [this](int item) {
		const float interval = (std::max)(autoInterval_, 1e-3f);
		return std::clamp((time_ - autoStartDelay_ - interval * static_cast<float>(item)) / interval + 1.0f, 0.0f, 1.0f);
	};
	aimDistance_ = aimDistancePx_ * progress(0);
	if (progress(0) >= 1.0f) Clear(Item::Aim);

	const float thruster = progress(1);
	for (int i = 0; i < kThrusterDirCount; ++i) {
		const float on = static_cast<float>(i + 1) / static_cast<float>(kThrusterDirCount);
		thrusterDone_[i] = thruster >= on;
		thrusterHeld_[i] = thruster >= on - 0.25f && thruster < on + 0.1f;
	}
	if (thruster >= 1.0f) Clear(Item::Thruster);

	weaponCount_ = static_cast<int>(std::floor(progress(2) * static_cast<float>(weaponShots_)));
	if (weaponCount_ >= weaponShots_) Clear(Item::Weapon);
}

void LaunchChecklist::LoadTuning(const JsonValue& node) {
	if (!node.IsObject()) return;
	auto read = [&node](const char* key, float& value) {
		value = static_cast<float>(node[key].AsDouble(value));
	};
	read("aimDistancePx", aimDistancePx_);
	read("aimStickSpeed", aimStickSpeed_);
	read("stickThreshold", stickThreshold_);
	weaponShots_ = (std::max)(1, static_cast<int>(node["weaponShots"].AsDouble(weaponShots_)));
	read("autoStartDelay", autoStartDelay_);
	read("autoInterval", autoInterval_);
}

void LaunchChecklist::SaveTuning(JsonValue& node) const {
	node = JsonValue::MakeObject();
	auto write = [&node](const char* key, float value) {
		node[key] = JsonValue(static_cast<double>(value));
	};
	write("aimDistancePx", aimDistancePx_);
	write("aimStickSpeed", aimStickSpeed_);
	write("stickThreshold", stickThreshold_);
	node["weaponShots"] = JsonValue(static_cast<double>(weaponShots_));
	write("autoStartDelay", autoStartDelay_);
	write("autoInterval", autoInterval_);
}

void LaunchChecklist::OnImGui() {
#ifdef _DEBUG
	ImGui::Text("aim %.0f / %.0f px  thruster %d%d%d%d  weapon %d / %d  clear %d / %d", aimDistance_, aimDistancePx_,
		thrusterDone_[0], thrusterDone_[1], thrusterDone_[2], thrusterDone_[3], weaponCount_, weaponShots_,
		ClearedCount(), kItemCount);
	if (ImGui::Button("Reset##checklist")) Reset();
	ImGui::SameLine();
	if (ImGui::Button("Clear All##checklist")) {
		aimDistance_ = aimDistancePx_;
		thrusterDone_.fill(true);
		weaponCount_ = weaponShots_;
		for (int i = 0; i < kItemCount; ++i) Clear(static_cast<Item>(i));
	}
	ImGui::DragFloat("Aim Distance (px)", &aimDistancePx_, 5.0f, 1.0f, 10000.0f);
	ImGui::DragFloat("Aim Stick Speed (px/s)", &aimStickSpeed_, 5.0f, 0.0f, 10000.0f);
	ImGui::SliderFloat("Stick Threshold", &stickThreshold_, 0.05f, 1.0f);
	ImGui::SliderInt("Weapon Shots", &weaponShots_, 1, 10);
	ImGui::DragFloat("Auto Start Delay (s)", &autoStartDelay_, 0.01f, 0.0f, 10.0f);
	ImGui::DragFloat("Auto Interval (s)", &autoInterval_, 0.01f, 0.05f, 10.0f);
#endif // _DEBUG
}
