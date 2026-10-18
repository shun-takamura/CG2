#include "HubBoardingSequence.h"

#include "Cockpit/CockpitModel.h"
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
	const Vector3 seat = CockpitModel::GetSeatedEye();
	// 機体の原点からの相対（正面 +Z）。踊り場と足場は高さ 10.6m で、目はその 1.6m 上。足場の先は z 3.85（17 番 §2.1）。
	// 2026-10-18 機体を自機の新モデルに替え、「搭乗」で手を差し出す（Offer）：手のひらの上面は 11.0m（足場の 0.4m 上）・
	// z 2.2〜3.8。胸の装甲の前面は z 約 2.4、コクピットの入口は z 1.565。座った目は CockpitModel::GetSeatedEye()（17 番 §5.1）
	track_.SetKeys({
		// 待機：足場の上から機体の胸を見る
		{ 0.0f, { 0.0f, 12.2f, 6.6f }, 180.0f, -2.0f, Ease::Linear, false },
		// 機体が手を差し出すのを見下ろして待つ
		{ 1.6f, { 0.0f, 12.2f, 6.6f }, 180.0f, 6.0f, Ease::InOut, false },
		// 足場の上を歩く。手のひらを見下ろす
		{ 2.4f, { 0.0f, 12.2f, 5.2f }, 180.0f, 14.0f, Ease::In, true },
		// 足場の先へ（胸の装甲が光って開く）
		{ 3.2f, { 0.0f, 12.2f, 3.9f }, 180.0f, 16.0f, Ease::Linear, true },
		// 手のひらへ上がる
		{ 3.6f, { 0.0f, 12.6f, 3.4f }, 180.0f, 12.0f, Ease::Linear, true },
		// 手のひらを歩いて胸の入口の前へ
		{ 4.6f, { 0.0f, 12.6f, 2.7f }, 180.0f, 4.0f, Ease::Linear, true },
		// 入口をくぐって中へ
		{ 5.4f, { 0.0f, 12.7f, 1.4f }, 180.0f, 0.0f, Ease::Out, true },
		// 振り返って座る。あとは正面を向いたまま（ハッチは見上げない）
		{ 6.6f, seat, 360.0f, 0.0f, Ease::InOut, false },
	});
	track_.SetBob(0.04f, 1.8f);
	offerStartTime_ = 0.0f;
	chestOpenStartTime_ = 2.6f;
	chestOpenEndTime_ = 3.6f;
	chestCloseStartTime_ = 6.8f;
	chestCloseEndTime_ = 7.6f;
	sitTime_ = 6.6f;
	sitShakeIntensity_ = 0.025f;
	sitShakeSeconds_ = 0.25f;
	hatchStartTime_ = 7.0f;
	hatchCloseTime_ = 9.2f;
	hatchCloseExponent_ = 2.0f;
	closeShakeIntensity_ = 0.04f;
	closeShakeSeconds_ = 0.35f;
	lightDimStartRatio_ = 0.4f;
	minLightScale_ = 0.1f;
	endTime_ = 9.3f;
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

	// 機体：手を差し出し（最後のコマで止まる）、胸の装甲が開いて、座った後に閉じる
	pose.mech.arm = HubMechModel::ArmClip::Offer;
	pose.mech.armTime = time - offerStartTime_;
	const float open = SmoothStep01((time - chestOpenStartTime_) / (std::max)(chestOpenEndTime_ - chestOpenStartTime_, 1e-4f));
	const float close = SmoothStep01((time - chestCloseStartTime_) / (std::max)(chestCloseEndTime_ - chestCloseStartTime_, 1e-4f));
	pose.mech.chestOpen = open * (1.0f - close);
	return pose;
}

void HubBoardingSequence::LoadTuning(const JsonValue& node) {
	if (!node.IsObject()) return;
	auto readFloat = [&node](const char* key, float& value) {
		value = static_cast<float>(node[key].AsDouble(value));
	};
	readFloat("offerStartTime", offerStartTime_);
	readFloat("chestOpenStartTime", chestOpenStartTime_);
	readFloat("chestOpenEndTime", chestOpenEndTime_);
	readFloat("chestCloseStartTime", chestCloseStartTime_);
	readFloat("chestCloseEndTime", chestCloseEndTime_);
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
	node["offerStartTime"] = JsonValue(static_cast<double>(offerStartTime_));
	node["chestOpenStartTime"] = JsonValue(static_cast<double>(chestOpenStartTime_));
	node["chestOpenEndTime"] = JsonValue(static_cast<double>(chestOpenEndTime_));
	node["chestCloseStartTime"] = JsonValue(static_cast<double>(chestCloseStartTime_));
	node["chestCloseEndTime"] = JsonValue(static_cast<double>(chestCloseEndTime_));
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
	ImGui::DragFloat("Offer Start (hand)", &offerStartTime_, 0.01f, 0.0f, 30.0f);
	ImGui::DragFloat("Chest Open Start", &chestOpenStartTime_, 0.01f, 0.0f, 30.0f);
	ImGui::DragFloat("Chest Open End", &chestOpenEndTime_, 0.01f, 0.0f, 30.0f);
	ImGui::DragFloat("Chest Close Start", &chestCloseStartTime_, 0.01f, 0.0f, 30.0f);
	ImGui::DragFloat("Chest Close End", &chestCloseEndTime_, 0.01f, 0.0f, 30.0f);
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
