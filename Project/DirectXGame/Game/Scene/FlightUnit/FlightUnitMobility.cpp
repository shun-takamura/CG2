#include "FlightUnitMobility.h"

#include "Json/JsonValue.h"
#include <algorithm>

#ifdef _DEBUG
#include "imgui.h"
#endif

bool FlightUnitMobility::Activate() {
	if (!host_) return false;
	// ほかの演出が World を握っている間は出さない（ジャスト回避・必殺技・Aim オーサリング）
	if (host_->GetTimeScale(TimeGroup::World) < 0.999f) return false;
	active_ = true;
	timer_ = 0.0f;
	return true;
}

void FlightUnitMobility::Cancel(bool restoreTime) {
	if (!active_) return;
	active_ = false;
	timer_ = 0.0f;
	if (restoreTime && host_) host_->SetTimeScale(TimeGroup::World, 1.0f);
}

float FlightUnitMobility::GetActiveRatio() const {
	const float d = slowDuration_[LevelIndex()];
	return d > 1e-4f ? std::clamp(1.0f - timer_ / d, 0.0f, 1.0f) : 0.0f;
}

void FlightUnitMobility::Update(float dt, bool canAct) {
	(void)canAct;
	if (!active_ || !host_) return;
	timer_ += dt;
	const float d = slowDuration_[LevelIndex()];
	if (timer_ >= d) {
		Cancel(true);
		return;
	}
	// 入り・抜けを短く補間する（1.0 ⇄ slowWorld_）
	float k = 1.0f;
	if (easeIn_ > 1e-4f && timer_ < easeIn_) k = timer_ / easeIn_;
	if (easeOut_ > 1e-4f && d - timer_ < easeOut_) k = (std::min)(k, (d - timer_) / easeOut_);
	host_->SetTimeScale(TimeGroup::World, 1.0f + (slowWorld_ - 1.0f) * k);
}

void FlightUnitMobility::LoadTuning(const JsonValue& node) {
	ReadLevels(node, "moveSpeedScale", moveSpeedScale_);
	ReadLevels(node, "justDodgeGaugeBonus", justDodgeGaugeBonus_);
	ReadLevels(node, "justDodgeWindowBonus", justDodgeWindowBonus_);
	ReadLevels(node, "slowDuration", slowDuration_);
	ReadLevels(node, "cooldown", cooldown_);
	slowWorld_ = static_cast<float>(node["slowWorld"].AsDouble(slowWorld_));
	easeIn_ = static_cast<float>(node["easeIn"].AsDouble(easeIn_));
	easeOut_ = static_cast<float>(node["easeOut"].AsDouble(easeOut_));
	ReadVec4(node, "color", color_);
}

void FlightUnitMobility::SaveTuning(JsonValue& node) const {
	WriteLevels(node, "moveSpeedScale", moveSpeedScale_);
	WriteLevels(node, "justDodgeGaugeBonus", justDodgeGaugeBonus_);
	WriteLevels(node, "justDodgeWindowBonus", justDodgeWindowBonus_);
	WriteLevels(node, "slowDuration", slowDuration_);
	WriteLevels(node, "cooldown", cooldown_);
	node["slowWorld"] = JsonValue(static_cast<double>(slowWorld_));
	node["easeIn"] = JsonValue(static_cast<double>(easeIn_));
	node["easeOut"] = JsonValue(static_cast<double>(easeOut_));
	WriteVec4(node, "color", color_);
}

void FlightUnitMobility::OnImGui() {
#ifdef _DEBUG
	DragLevels("Move Speed Scale##Mobility", moveSpeedScale_, 0.01f, 0.5f, 3.0f);
	DragLevels("JustDodge Gauge Bonus##Mobility", justDodgeGaugeBonus_, 0.1f, 0.0f, 100.0f);
	DragLevels("JustDodge Window Bonus##Mobility", justDodgeWindowBonus_, 0.001f, 0.0f, 0.2f);
	DragLevels("Slow Duration##Mobility", slowDuration_, 0.05f, 0.1f, 20.0f);
	DragLevels("Cooldown##Mobility", cooldown_, 0.1f, 0.0f, 120.0f);
	ImGui::SliderFloat("Slow World##Mobility", &slowWorld_, 0.05f, 1.0f, "%.2f");
	ImGui::DragFloat("Ease In##Mobility", &easeIn_, 0.01f, 0.0f, 2.0f, "%.2f");
	ImGui::DragFloat("Ease Out##Mobility", &easeOut_, 0.01f, 0.0f, 2.0f, "%.2f");
	ImGui::ColorEdit4("Color##Mobility", &color_.x);
#endif
}
