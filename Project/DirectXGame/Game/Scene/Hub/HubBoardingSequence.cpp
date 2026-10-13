#include "HubBoardingSequence.h"

#include "Json/JsonValue.h"
#include <algorithm>
#include <cmath>

#ifdef _DEBUG
#include "imgui.h"
#endif

namespace {
	float Clamp01(float t) {
		return std::clamp(t, 0.0f, 1.0f);
	}

	float SmoothStep01(float t) {
		t = Clamp01(t);
		return t * t * (3.0f - 2.0f * t);
	}
}

HubBoardingSequence::HubBoardingSequence() {
	ResetToDefaults();
}

void HubBoardingSequence::ResetToDefaults() {
	using Ease = HubCameraTrack::Ease;
	// 機体の原点からの相対（正面 +Z）。回廊は高さ 10.6m（手のひらと同じ）で、目はその 1.6m 上。
	// 手のひらは (0, 10.57, 2.96) 付近、座った目は (0, 12.543, 0.985)（17 番 §5.1）
	track_.SetKeys({
		// 待機：手のひらの手前の足場の上から開いた胸を見る
		{ 0.0f, { 0.0f, 12.2f, 6.6f }, 180.0f, -2.0f, Ease::Linear, false },
		// 足場の端へ歩く。手のひらを見下ろす
		{ 0.8f, { 0.0f, 12.2f, 6.0f }, 180.0f, 16.0f, Ease::In, true },
		// 指先へ一段降りて手のひらに乗る
		{ 1.6f, { 0.0f, 12.1f, 5.4f }, 180.0f, 22.0f, Ease::Linear, true },
		// 手のひらを歩いて開口部の前へ
		{ 3.2f, { 0.0f, 12.2f, 3.4f }, 180.0f, 4.0f, Ease::Linear, true },
		// 開口部をくぐって中へ
		{ 4.0f, { 0.0f, 12.6f, 1.6f }, 180.0f, 0.0f, Ease::Out, true },
		// 振り返って座る。あとは正面を向いたまま（ハッチは見上げない）
		{ 5.2f, { 0.0f, 12.543f, 0.985f }, 360.0f, 0.0f, Ease::InOut, false },
	});
	track_.SetBob(0.04f, 1.8f);
	sitTime_ = 5.2f;
	sitShakeIntensity_ = 0.025f;
	sitShakeSeconds_ = 0.25f;
	hatchStartTime_ = 5.6f;
	hatchCloseTime_ = 7.8f;
	hatchCloseExponent_ = 2.0f;
	closeShakeIntensity_ = 0.04f;
	closeShakeSeconds_ = 0.35f;
	lightDimStartRatio_ = 0.4f;
	minLightScale_ = 0.1f;
	endTime_ = 7.9f;
}

HubCameraTrack::Key HubBoardingSequence::GetHomeKey() const {
	const auto& keys = track_.GetKeys();
	return keys.empty() ? HubCameraTrack::Key{} : keys.front();
}

void HubBoardingSequence::Start() {
	time_ = 0.0f;
	playing_ = true;
	sitImpactPending_ = false;
	closeImpactPending_ = false;
	previewing_ = false;
}

void HubBoardingSequence::Stop() {
	time_ = 0.0f;
	playing_ = false;
	sitImpactPending_ = false;
	closeImpactPending_ = false;
}

void HubBoardingSequence::Update(float deltaTime) {
	if (!playing_) return;
	const float prev = time_;
	time_ += deltaTime;
	if (prev < sitTime_ && time_ >= sitTime_) sitImpactPending_ = true;
	if (prev < hatchCloseTime_ && time_ >= hatchCloseTime_) closeImpactPending_ = true;
}

bool HubBoardingSequence::ConsumeSitImpact() {
	const bool pending = sitImpactPending_;
	sitImpactPending_ = false;
	return pending;
}

bool HubBoardingSequence::ConsumeCloseImpact() {
	const bool pending = closeImpactPending_;
	closeImpactPending_ = false;
	return pending;
}

HubBoardingSequence::Pose HubBoardingSequence::Evaluate(float time) const {
	Pose pose{};
	const HubCameraTrack::Sample camera = track_.Evaluate(time);
	pose.eye = camera.eye;
	pose.yaw = camera.yaw;
	pose.pitch = camera.pitch;

	// ハッチ：ゆっくり降り始めて、最後に勢いよく閉まる
	const float hatchSpan = (std::max)(hatchCloseTime_ - hatchStartTime_, 1e-4f);
	const float closeProgress = Clamp01((time - hatchStartTime_) / hatchSpan);
	pose.hatchOpenRatio = 1.0f - std::pow(closeProgress, (std::max)(hatchCloseExponent_, 0.01f));

	// 外の光：隙間が狭まると弱まる
	const float dimStart = (std::max)(lightDimStartRatio_, 1e-4f);
	pose.lightScale = minLightScale_ + (1.0f - minLightScale_) * SmoothStep01(pose.hatchOpenRatio / dimStart);
	return pose;
}

void HubBoardingSequence::LoadTuning(const JsonValue& node) {
	if (!node.IsObject()) return;
	auto readFloat = [&node](const char* key, float& value) {
		value = static_cast<float>(node[key].AsDouble(value));
	};
	readFloat("sitTime", sitTime_);
	readFloat("sitShakeIntensity", sitShakeIntensity_);
	readFloat("sitShakeSeconds", sitShakeSeconds_);
	readFloat("hatchStartTime", hatchStartTime_);
	readFloat("hatchCloseTime", hatchCloseTime_);
	readFloat("hatchCloseExponent", hatchCloseExponent_);
	readFloat("closeShakeIntensity", closeShakeIntensity_);
	readFloat("closeShakeSeconds", closeShakeSeconds_);
	readFloat("lightDimStartRatio", lightDimStartRatio_);
	readFloat("minLightScale", minLightScale_);
	readFloat("endTime", endTime_);
	track_.LoadTuning(node);
}

void HubBoardingSequence::SaveTuning(JsonValue& node) const {
	node = JsonValue::MakeObject();
	node["sitTime"] = JsonValue(static_cast<double>(sitTime_));
	node["sitShakeIntensity"] = JsonValue(static_cast<double>(sitShakeIntensity_));
	node["sitShakeSeconds"] = JsonValue(static_cast<double>(sitShakeSeconds_));
	node["hatchStartTime"] = JsonValue(static_cast<double>(hatchStartTime_));
	node["hatchCloseTime"] = JsonValue(static_cast<double>(hatchCloseTime_));
	node["hatchCloseExponent"] = JsonValue(static_cast<double>(hatchCloseExponent_));
	node["closeShakeIntensity"] = JsonValue(static_cast<double>(closeShakeIntensity_));
	node["closeShakeSeconds"] = JsonValue(static_cast<double>(closeShakeSeconds_));
	node["lightDimStartRatio"] = JsonValue(static_cast<double>(lightDimStartRatio_));
	node["minLightScale"] = JsonValue(static_cast<double>(minLightScale_));
	node["endTime"] = JsonValue(static_cast<double>(endTime_));
	track_.SaveTuning(node);
}

void HubBoardingSequence::OnImGui() {
#ifdef _DEBUG
	ImGui::Text("Time: %.2f / %.2f s  %s", time_, endTime_, playing_ ? "(playing)" : "");
	ImGui::Checkbox("Preview (scrub)", &previewing_);
	ImGui::SameLine();
	ImGui::SetNextItemWidth(-1.0f);
	ImGui::SliderFloat("##PreviewTime", &previewTime_, 0.0f, endTime_, "%.2f s");

	if (ImGui::Button("Reset To Defaults")) ResetToDefaults();

	ImGui::SeparatorText("Timing");
	ImGui::DragFloat("Sit Time", &sitTime_, 0.01f, 0.0f, 30.0f);
	ImGui::DragFloat("Sit Shake", &sitShakeIntensity_, 0.001f, 0.0f, 0.5f);
	ImGui::DragFloat("Sit Shake (s)", &sitShakeSeconds_, 0.01f, 0.0f, 2.0f);
	ImGui::DragFloat("Hatch Start", &hatchStartTime_, 0.01f, 0.0f, 30.0f);
	ImGui::DragFloat("Hatch Close", &hatchCloseTime_, 0.01f, 0.0f, 30.0f);
	ImGui::DragFloat("Hatch Exponent", &hatchCloseExponent_, 0.01f, 0.1f, 8.0f);
	ImGui::DragFloat("Close Shake", &closeShakeIntensity_, 0.001f, 0.0f, 0.5f);
	ImGui::DragFloat("Close Shake (s)", &closeShakeSeconds_, 0.01f, 0.0f, 2.0f);
	ImGui::DragFloat("Light Dim Start Ratio", &lightDimStartRatio_, 0.01f, 0.01f, 1.0f);
	ImGui::DragFloat("Min Light Scale", &minLightScale_, 0.01f, 0.0f, 1.0f);
	ImGui::DragFloat("End (to cockpit)", &endTime_, 0.01f, 0.0f, 30.0f);

	if (track_.OnImGui(previewTime_)) previewing_ = true;
#endif // _DEBUG
}
