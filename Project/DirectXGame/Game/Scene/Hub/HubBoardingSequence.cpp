#include "HubBoardingSequence.h"

#include "Cockpit/CockpitModel.h"
#include "Json/JsonValue.h"
#include <algorithm>
#include <cmath>

#ifdef _DEBUG
#include "imgui.h"
#endif

namespace {
	// smoothstep（3t² − 2t³）の 2 乗の項の係数
	constexpr float kSmoothStepSquareCoef = 3.0f;
	// smoothstep（3t² − 2t³）の 3 乗の項の係数
	constexpr float kSmoothStepCubeCoef = 2.0f;
	// 待機の目の位置：足場の上から機体の胸を見る（機体の原点から）[m]
	constexpr Vector3 kHomeEye = { 0.0f, 12.2f, 6.6f };
	// 機体の方（−Z）を向く yaw [度]
	constexpr float kFaceMechYawDegrees = 180.0f;
	// 待機で見上げる角度 [度]（− で上を向く）
	constexpr float kHomePitchDegrees = 2.0f;
	// 機体が手を差し出すのを見下ろして待つキーの時刻 [秒]
	constexpr float kWatchOfferTime = 1.6f;
	// 手を差し出すのを見下ろす角度 [度]
	constexpr float kWatchOfferPitchDegrees = 6.0f;
	// 足場の上を歩くキーの時刻 [秒]
	constexpr float kWalkBridgeTime = 2.4f;
	// 足場の上を歩く所の目の位置 [m]
	constexpr Vector3 kWalkBridgeEye = { 0.0f, 12.2f, 5.2f };
	// 足場の上で手のひらを見下ろす角度 [度]
	constexpr float kWalkBridgePitchDegrees = 14.0f;
	// 足場の先へ着くキーの時刻 [秒]
	constexpr float kBridgeTipTime = 3.2f;
	// 足場の先の目の位置 [m]
	constexpr Vector3 kBridgeTipEye = { 0.0f, 12.2f, 3.9f };
	// 足場の先で見下ろす角度 [度]
	constexpr float kBridgeTipPitchDegrees = 16.0f;
	// 手のひらへ上がるキーの時刻 [秒]
	constexpr float kStepOnPalmTime = 3.6f;
	// 手のひらへ上がった所の目の位置 [m]
	constexpr Vector3 kStepOnPalmEye = { 0.0f, 12.6f, 3.4f };
	// 手のひらへ上がった所で見下ろす角度 [度]
	constexpr float kStepOnPalmPitchDegrees = 12.0f;
	// 手のひらを歩いて胸の入口の前へ着くキーの時刻 [秒]
	constexpr float kPalmTime = 4.6f;
	// 胸の入口の前の目の位置 [m]
	constexpr Vector3 kPalmEye = { 0.0f, 12.6f, 2.7f };
	// 胸の入口の前で見下ろす角度 [度]
	constexpr float kPalmPitchDegrees = 4.0f;
	// 入口をくぐって中へ入るキーの時刻 [秒]
	constexpr float kEnterTime = 5.4f;
	// 入口をくぐった所の目の位置 [m]
	constexpr Vector3 kEnterEye = { 0.0f, 12.7f, 1.4f };
	// 振り返って座る時刻 [秒]
	constexpr float kSitTime = 6.6f;
	// 振り返って正面を向いた yaw [度]
	constexpr float kFaceForwardYawDegrees = 360.0f;
	// 歩くときの頭の上下の幅 [m]
	constexpr float kBobAmplitude = 0.04f;
	// 歩くときの 1 秒あたりの歩数
	constexpr float kBobStepsPerSecond = 1.8f;
	// 胸の装甲が開き始める時刻の初期値 [秒]
	constexpr float kDefaultChestOpenStartTime = 2.6f;
	// 胸の装甲が開き切る時刻の初期値 [秒]
	constexpr float kDefaultChestOpenEndTime = 3.6f;
	// 胸の装甲が閉じ始める時刻の初期値 [秒]
	constexpr float kDefaultChestCloseStartTime = 6.8f;
	// 胸の装甲が閉じ切る時刻の初期値 [秒]
	constexpr float kDefaultChestCloseEndTime = 7.6f;
	// 座ったときの揺れの強さの初期値
	constexpr float kDefaultSitShakeIntensity = 0.025f;
	// 座ったときの揺れの長さの初期値 [秒]
	constexpr float kDefaultSitShakeSeconds = 0.25f;
	// ハッチが降り始める時刻の初期値 [秒]
	constexpr float kDefaultHatchStartTime = 7.0f;
	// ハッチが閉まる時刻の初期値 [秒]
	constexpr float kDefaultHatchCloseTime = 9.2f;
	// ハッチの閉まり方の指数の初期値（大きいほど最後に勢いよく閉まる）
	constexpr float kDefaultHatchCloseExponent = 2.0f;
	// ハッチが閉まったときの揺れの強さの初期値
	constexpr float kDefaultCloseShakeIntensity = 0.04f;
	// ハッチが閉まったときの揺れの長さの初期値 [秒]
	constexpr float kDefaultCloseShakeSeconds = 0.35f;
	// 外の光が弱まり始めるハッチの開きの割合の初期値
	constexpr float kDefaultLightDimStartRatio = 0.4f;
	// ハッチが閉まり切ったときの外の光の倍率の初期値
	constexpr float kDefaultMinLightScale = 0.1f;
	// コクピットへ切り替わる時刻の初期値 [秒]
	constexpr float kDefaultEndTime = 9.3f;
	// 区間の長さ・割合の下限（0 で割らないように）
	constexpr float kMinSpanSeconds = 1e-4f;
	// ハッチの閉まり方の指数の下限
	constexpr float kMinHatchCloseExponent = 0.01f;

	/// <summary>0〜1 に切る</summary>
	/// <param name="t">入力</param>
	/// <returns>0〜1 の値</returns>
	float Clamp01(float t) {
		return std::clamp(t, 0.0f, 1.0f);
	}

	/// <summary>0〜1 に切ってから smoothstep をかける</summary>
	/// <param name="t">入力</param>
	/// <returns>0〜1 のなめらかな値</returns>
	float SmoothStep01(float t) {
		t = Clamp01(t);
		return t * t * (kSmoothStepSquareCoef - kSmoothStepCubeCoef * t);
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
		{ 0.0f, kHomeEye, kFaceMechYawDegrees, -kHomePitchDegrees, Ease::Linear, false },
		// 機体が手を差し出すのを見下ろして待つ
		{ kWatchOfferTime, kHomeEye, kFaceMechYawDegrees, kWatchOfferPitchDegrees, Ease::InOut, false },
		// 足場の上を歩く。手のひらを見下ろす
		{ kWalkBridgeTime, kWalkBridgeEye, kFaceMechYawDegrees, kWalkBridgePitchDegrees, Ease::In, true },
		// 足場の先へ（胸の装甲が光って開く）
		{ kBridgeTipTime, kBridgeTipEye, kFaceMechYawDegrees, kBridgeTipPitchDegrees, Ease::Linear, true },
		// 手のひらへ上がる
		{ kStepOnPalmTime, kStepOnPalmEye, kFaceMechYawDegrees, kStepOnPalmPitchDegrees, Ease::Linear, true },
		// 手のひらを歩いて胸の入口の前へ
		{ kPalmTime, kPalmEye, kFaceMechYawDegrees, kPalmPitchDegrees, Ease::Linear, true },
		// 入口をくぐって中へ
		{ kEnterTime, kEnterEye, kFaceMechYawDegrees, 0.0f, Ease::Out, true },
		// 振り返って座る。あとは正面を向いたまま（ハッチは見上げない）
		{ kSitTime, seat, kFaceForwardYawDegrees, 0.0f, Ease::InOut, false },
	});
	track_.SetBob(kBobAmplitude, kBobStepsPerSecond);
	offerStartTime_ = 0.0f;
	chestOpenStartTime_ = kDefaultChestOpenStartTime;
	chestOpenEndTime_ = kDefaultChestOpenEndTime;
	chestCloseStartTime_ = kDefaultChestCloseStartTime;
	chestCloseEndTime_ = kDefaultChestCloseEndTime;
	sitTime_ = kSitTime;
	sitShakeIntensity_ = kDefaultSitShakeIntensity;
	sitShakeSeconds_ = kDefaultSitShakeSeconds;
	hatchStartTime_ = kDefaultHatchStartTime;
	hatchCloseTime_ = kDefaultHatchCloseTime;
	hatchCloseExponent_ = kDefaultHatchCloseExponent;
	closeShakeIntensity_ = kDefaultCloseShakeIntensity;
	closeShakeSeconds_ = kDefaultCloseShakeSeconds;
	lightDimStartRatio_ = kDefaultLightDimStartRatio;
	minLightScale_ = kDefaultMinLightScale;
	endTime_ = kDefaultEndTime;
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
	const float hatchSpan = (std::max)(hatchCloseTime_ - hatchStartTime_, kMinSpanSeconds);
	const float closeProgress = Clamp01((time - hatchStartTime_) / hatchSpan);
	pose.hatchOpenRatio = 1.0f - std::pow(closeProgress, (std::max)(hatchCloseExponent_, kMinHatchCloseExponent));

	// 外の光：隙間が狭まると弱まる
	const float dimStart = (std::max)(lightDimStartRatio_, kMinSpanSeconds);
	pose.lightScale = minLightScale_ + (1.0f - minLightScale_) * SmoothStep01(pose.hatchOpenRatio / dimStart);

	// 機体：手を差し出し（最後のコマで止まる）、胸の装甲が開いて、座った後に閉じる
	pose.mech.arm = HubMechModel::ArmClip::Offer;
	pose.mech.armTime = time - offerStartTime_;
	const float open = SmoothStep01((time - chestOpenStartTime_) / (std::max)(chestOpenEndTime_ - chestOpenStartTime_, kMinSpanSeconds));
	const float close = SmoothStep01((time - chestCloseStartTime_) / (std::max)(chestCloseEndTime_ - chestCloseStartTime_, kMinSpanSeconds));
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
