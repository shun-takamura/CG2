#include "HubDisembarkSequence.h"

#include "Cockpit/CockpitModel.h"
#include "Json/JsonValue.h"
#include <algorithm>

#ifdef _DEBUG
#include "imgui.h"
#endif

namespace {
	// smoothstep（3t² − 2t³）の 2 乗の項の係数
	constexpr float kSmoothStepSquareCoef = 3.0f;
	// smoothstep（3t² − 2t³）の 3 乗の項の係数
	constexpr float kSmoothStepCubeCoef = 2.0f;
	// 座った目と同じ外（+Z）を向く yaw [度]
	constexpr float kFaceForwardYawDegrees = 360.0f;
	// 座ったままハッチが開くのを待ち終えるキーの時刻 [秒]
	constexpr float kWaitHatchEndTime = 2.2f;
	// 立ち上がるキーの時刻 [秒]
	constexpr float kStandUpTime = 2.7f;
	// 立ち上がった目の高さ（機体の原点から）[m]
	constexpr float kStandUpEyeY = 12.75f;
	// 立ち上がったときに座った目から前へ出る距離 [m]
	constexpr float kStandUpForward = 0.27f;
	// 立ち上がった所で見下ろす角度 [度]
	constexpr float kStandUpPitchDegrees = 5.0f;
	// 入口をくぐって胸の装甲の外へ出るキーの時刻 [秒]
	constexpr float kExitChestTime = 3.5f;
	// 胸の装甲の外へ出た所の目の位置 [m]
	constexpr Vector3 kExitChestEye = { 0.0f, 12.7f, 1.7f };
	// 胸の装甲の外へ出た所で見下ろす角度 [度]
	constexpr float kExitChestPitchDegrees = 8.0f;
	// 手のひらの手前側を歩くキーの時刻 [秒]
	constexpr float kPalmNearTime = 4.3f;
	// 手のひらの手前側の目の位置 [m]
	constexpr Vector3 kPalmNearEye = { 0.0f, 12.6f, 2.9f };
	// 手のひらの上で足元を見る角度 [度]
	constexpr float kPalmPitchDegrees = 14.0f;
	// 手のひらの先を歩くキーの時刻 [秒]
	constexpr float kPalmFarTime = 5.1f;
	// 手のひらの先の目の位置 [m]
	constexpr Vector3 kPalmFarEye = { 0.0f, 12.6f, 3.6f };
	// 手のひらから足場へ降りるキーの時刻 [秒]
	constexpr float kStepDownTime = 5.7f;
	// 足場へ降りた所の目の位置 [m]
	constexpr Vector3 kStepDownEye = { 0.0f, 12.2f, 4.2f };
	// 足場へ降りた所で見下ろす角度 [度]
	constexpr float kStepDownPitchDegrees = 12.0f;
	// 足場の上を歩くキーの時刻 [秒]
	constexpr float kWalkBridgeTime = 6.4f;
	// 足場の上を歩く所の目の位置 [m]
	constexpr Vector3 kWalkBridgeEye = { 0.0f, 12.2f, 5.2f };
	// 足場の上を歩く所で見下ろす角度 [度]
	constexpr float kWalkBridgePitchDegrees = 2.0f;
	// 待機の位置まで歩き終えるキーの時刻 [秒]
	constexpr float kWalkEndTime = 7.0f;
	// 待機の目の位置（Start で待機の視点に合わせる）[m]
	constexpr Vector3 kHomeEye = { 0.0f, 12.2f, 6.6f };
	// 振り返って機体を見るキーの時刻 [秒]
	constexpr float kTurnBackTime = 8.2f;
	// 振り返って機体の方を向いた yaw [度]（360 + 180）
	constexpr float kTurnBackYawDegrees = 540.0f;
	// 振り返って機体を見上げる角度 [度]（− で上を向く）
	constexpr float kHomePitchDegrees = 2.0f;
	// 歩くときの頭の上下の幅 [m]
	constexpr float kBobAmplitude = 0.04f;
	// 歩くときの 1 秒あたりの歩数
	constexpr float kBobStepsPerSecond = 1.8f;
	// ハッチが開き始める時刻の初期値 [秒]
	constexpr float kDefaultHatchStartTime = 0.2f;
	// 胸の装甲が開き始める時刻の初期値 [秒]
	constexpr float kDefaultChestOpenStartTime = 1.6f;
	// 胸の装甲が開き切る時刻の初期値 [秒]
	constexpr float kDefaultChestOpenEndTime = 2.6f;
	// 胸の装甲が閉じ始める時刻の初期値 [秒]
	constexpr float kDefaultChestCloseStartTime = 4.6f;
	// 胸の装甲が閉じ切る時刻の初期値 [秒]
	constexpr float kDefaultChestCloseEndTime = 5.4f;
	// 差し出した手を戻し始める時刻の初期値 [秒]
	constexpr float kDefaultRetractStartTime = 7.0f;
	// ハッチが開き切る時刻の初期値 [秒]
	constexpr float kDefaultHatchOpenTime = 2.0f;
	// 外の光が戻り切るハッチの開きの割合の初期値
	constexpr float kDefaultLightDimStartRatio = 0.4f;
	// HUB へ戻る時刻の初期値（手を戻し終える Retract 7.0〜8.4 秒まで）[秒]
	constexpr float kDefaultEndTime = 8.5f;
	// 待機の視点に合わせる最後のキーの数（歩き終わりと振り返り）
	constexpr int kHomeMatchedKeyCount = 2;
	// 一回りの角度 [度]
	constexpr float kFullTurnDegrees = 360.0f;
	// 区間の長さ・割合の下限（0 で割らないように）
	constexpr float kMinSpanSeconds = 1e-4f;
	// Offer の最後のコマに止めるための十分先の時刻 [秒]
	constexpr float kOfferLastFrameTime = 1e6f;

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

HubDisembarkSequence::HubDisembarkSequence() {
	ResetToDefaults();
}

void HubDisembarkSequence::ResetToDefaults() {
	using Ease = HubCameraTrack::Ease;
	const Vector3 seat = CockpitModel::GetSeatedEye();
	// 機体の原点からの相対（正面 +Z）。座った目は外（+Z）を向いているので、そのまま前へ歩けば機体に背を向けて出られる
	track_.SetKeys({
		// 座ったまま正面を向いてハッチが開くのを待つ（見上げない）
		{ 0.0f, seat, kFaceForwardYawDegrees, 0.0f, Ease::Linear, false },
		{ kWaitHatchEndTime, seat, kFaceForwardYawDegrees, 0.0f, Ease::Linear, false },
		// 立ち上がる
		{ kStandUpTime, { seat.x, kStandUpEyeY, seat.z + kStandUpForward }, kFaceForwardYawDegrees, kStandUpPitchDegrees, Ease::InOut, false },
		// 入口（z 1.565）をくぐり、光って開いた胸の装甲の外へ
		{ kExitChestTime, kExitChestEye, kFaceForwardYawDegrees, kExitChestPitchDegrees, Ease::Linear, true },
		// 差し出した手のひら（上面 11.0m・z 2.2〜3.8）を歩く（足元を見る）
		{ kPalmNearTime, kPalmNearEye, kFaceForwardYawDegrees, kPalmPitchDegrees, Ease::Linear, true },
		{ kPalmFarTime, kPalmFarEye, kFaceForwardYawDegrees, kPalmPitchDegrees, Ease::Linear, true },
		// 手のひらから足場（上面 10.6m・先 z 3.85）へ降りる
		{ kStepDownTime, kStepDownEye, kFaceForwardYawDegrees, kStepDownPitchDegrees, Ease::Linear, true },
		// 足場の上を歩く
		{ kWalkBridgeTime, kWalkBridgeEye, kFaceForwardYawDegrees, kWalkBridgePitchDegrees, Ease::Linear, true },
		// 足場の上の待機の位置まで（位置は Start で待機の視点に合わせる）
		{ kWalkEndTime, kHomeEye, kFaceForwardYawDegrees, 0.0f, Ease::Out, true },
		// 振り返って機体を見る（位置と向きは Start で待機の視点に合わせる）
		{ kTurnBackTime, kHomeEye, kTurnBackYawDegrees, -kHomePitchDegrees, Ease::InOut, false },
	});
	track_.SetBob(kBobAmplitude, kBobStepsPerSecond);
	hatchStartTime_ = kDefaultHatchStartTime;
	chestOpenStartTime_ = kDefaultChestOpenStartTime;
	chestOpenEndTime_ = kDefaultChestOpenEndTime;
	chestCloseStartTime_ = kDefaultChestCloseStartTime;
	chestCloseEndTime_ = kDefaultChestCloseEndTime;
	retractStartTime_ = kDefaultRetractStartTime;
	hatchOpenTime_ = kDefaultHatchOpenTime;
	lightDimStartRatio_ = kDefaultLightDimStartRatio;
	endTime_ = kDefaultEndTime;   // 手を戻し終える（Retract 7.0〜8.4 秒）まで
}

void HubDisembarkSequence::MatchHome(const HubCameraTrack::Key& home) {
	auto& keys = track_.GetKeys();
	if (keys.size() < kHomeMatchedKeyCount) return;
	HubCameraTrack::Key& walkEnd = keys[keys.size() - kHomeMatchedKeyCount];
	HubCameraTrack::Key& turn = keys.back();
	walkEnd.eye = home.eye;
	turn.eye = home.eye;
	// 待機の向きを、歩き終わりの向きより先（巻き戻さずに回す側）の同じ向きにそろえる
	float target = home.yawDegrees;
	while (target <= walkEnd.yawDegrees) target += kFullTurnDegrees;
	turn.yawDegrees = target;
	turn.pitchDegrees = home.pitchDegrees;
}

void HubDisembarkSequence::Start(const HubCameraTrack::Key& home, float minLightScale) {
	MatchHome(home);
	minLightScale_ = minLightScale;
	time_ = 0.0f;
	playing_ = true;
	previewing_ = false;
}

void HubDisembarkSequence::Stop() {
	time_ = 0.0f;
	playing_ = false;
}

void HubDisembarkSequence::Update(float deltaTime) {
	if (!playing_) return;
	time_ += deltaTime;
}

HubDisembarkSequence::Pose HubDisembarkSequence::Evaluate(float time) const {
	Pose pose{};
	const HubCameraTrack::Sample camera = track_.Evaluate(time);
	pose.eye = camera.eye;
	pose.yaw = camera.yaw;
	pose.pitch = camera.pitch;

	// ハッチ：ゆっくり動き出して、ゆっくり止まる
	const float span = (std::max)(hatchOpenTime_ - hatchStartTime_, kMinSpanSeconds);
	pose.hatchOpenRatio = SmoothStep01((time - hatchStartTime_) / span);

	// 外の光：隙間が広がると戻る（乗り込みと同じ式）
	const float dimStart = (std::max)(lightDimStartRatio_, kMinSpanSeconds);
	pose.lightScale = minLightScale_ + (1.0f - minLightScale_) * SmoothStep01(pose.hatchOpenRatio / dimStart);

	// 機体：差し出した手のまま（Offer の最後のコマ）→ 振り返る間に戻す。胸の装甲は出る間だけ開く
	if (time < retractStartTime_) {
		pose.mech.arm = HubMechModel::ArmClip::Offer;
		pose.mech.armTime = kOfferLastFrameTime;
	} else {
		pose.mech.arm = HubMechModel::ArmClip::Retract;
		pose.mech.armTime = time - retractStartTime_;
	}
	const float open = SmoothStep01((time - chestOpenStartTime_) / (std::max)(chestOpenEndTime_ - chestOpenStartTime_, kMinSpanSeconds));
	const float close = SmoothStep01((time - chestCloseStartTime_) / (std::max)(chestCloseEndTime_ - chestCloseStartTime_, kMinSpanSeconds));
	pose.mech.chestOpen = open * (1.0f - close);
	return pose;
}

void HubDisembarkSequence::LoadTuning(const JsonValue& node) {
	if (!node.IsObject()) return;
	auto readFloat = [&node](const char* key, float& value) {
		value = static_cast<float>(node[key].AsDouble(value));
	};
	readFloat("hatchStartTime", hatchStartTime_);
	readFloat("hatchOpenTime", hatchOpenTime_);
	readFloat("lightDimStartRatio", lightDimStartRatio_);
	readFloat("chestOpenStartTime", chestOpenStartTime_);
	readFloat("chestOpenEndTime", chestOpenEndTime_);
	readFloat("chestCloseStartTime", chestCloseStartTime_);
	readFloat("chestCloseEndTime", chestCloseEndTime_);
	readFloat("retractStartTime", retractStartTime_);
	readFloat("endTime", endTime_);
	track_.LoadTuning(node);
}

void HubDisembarkSequence::SaveTuning(JsonValue& node) const {
	node = JsonValue::MakeObject();
	node["hatchStartTime"] = JsonValue(static_cast<double>(hatchStartTime_));
	node["hatchOpenTime"] = JsonValue(static_cast<double>(hatchOpenTime_));
	node["lightDimStartRatio"] = JsonValue(static_cast<double>(lightDimStartRatio_));
	node["chestOpenStartTime"] = JsonValue(static_cast<double>(chestOpenStartTime_));
	node["chestOpenEndTime"] = JsonValue(static_cast<double>(chestOpenEndTime_));
	node["chestCloseStartTime"] = JsonValue(static_cast<double>(chestCloseStartTime_));
	node["chestCloseEndTime"] = JsonValue(static_cast<double>(chestCloseEndTime_));
	node["retractStartTime"] = JsonValue(static_cast<double>(retractStartTime_));
	node["endTime"] = JsonValue(static_cast<double>(endTime_));
	track_.SaveTuning(node);
}

void HubDisembarkSequence::OnImGui(const HubCameraTrack::Key& home, float minLightScale) {
#ifdef _DEBUG
	ImGui::Text("Time: %.2f / %.2f s  %s", time_, endTime_, playing_ ? "(playing)" : "");
	if (ImGui::Checkbox("Preview (scrub)", &previewing_) && previewing_) {
		MatchHome(home);
		minLightScale_ = minLightScale;
	}
	ImGui::SameLine();
	ImGui::SetNextItemWidth(-1.0f);
	ImGui::SliderFloat("##DisembarkPreviewTime", &previewTime_, 0.0f, endTime_, "%.2f s");

	if (ImGui::Button("Reset To Defaults")) ResetToDefaults();

	ImGui::SeparatorText("Timing");
	ImGui::DragFloat("Hatch Start", &hatchStartTime_, 0.01f, 0.0f, 30.0f);
	ImGui::DragFloat("Hatch Open", &hatchOpenTime_, 0.01f, 0.0f, 30.0f);
	ImGui::DragFloat("Light Dim Start Ratio", &lightDimStartRatio_, 0.01f, 0.01f, 1.0f);
	ImGui::DragFloat("Chest Open Start", &chestOpenStartTime_, 0.01f, 0.0f, 30.0f);
	ImGui::DragFloat("Chest Open End", &chestOpenEndTime_, 0.01f, 0.0f, 30.0f);
	ImGui::DragFloat("Chest Close Start", &chestCloseStartTime_, 0.01f, 0.0f, 30.0f);
	ImGui::DragFloat("Chest Close End", &chestCloseEndTime_, 0.01f, 0.0f, 30.0f);
	ImGui::DragFloat("Retract Start (hand)", &retractStartTime_, 0.01f, 0.0f, 30.0f);
	ImGui::DragFloat("End (to waiting)", &endTime_, 0.01f, 0.0f, 30.0f);
	ImGui::TextDisabled("Last 2 keys' eye and the last key's yaw/pitch follow the waiting view (boarding key 0).");

	if (track_.OnImGui(previewTime_)) {
		previewing_ = true;
		MatchHome(home);
		minLightScale_ = minLightScale;
	}
#endif // _DEBUG
}
