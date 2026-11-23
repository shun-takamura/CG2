#include "HubEntrySequence.h"

#include "Json/JsonValue.h"

#ifdef _DEBUG
#include "imgui.h"
#endif

namespace {
	// 機体の方（−Z）を向く yaw [度]
	constexpr float kFaceMechYawDegrees = 180.0f;
	// 閉じたタイトルの扉のすぐ手前の目の位置（機体の原点から）[m]
	constexpr Vector3 kDoorEye = { 0.0f, 12.2f, 17.8f };
	// 扉の手前で見上げる角度 [度]（− で上を向く）
	constexpr float kDoorPitchDegrees = 6.0f;
	// 踊り場を歩き出すキーの時刻 [秒]
	constexpr float kWalkStartTime = 1.6f;
	// 踊り場を歩き出す所の目の位置 [m]
	constexpr Vector3 kWalkStartEye = { 0.0f, 12.2f, 15.2f };
	// 踊り場を歩き出す所で見上げる角度 [度]（− で上を向く）
	constexpr float kWalkStartPitchDegrees = 3.0f;
	// 足場の上を進むキーの時刻 [秒]
	constexpr float kBridgeTime = 4.0f;
	// 足場の上を進む所の目の位置 [m]
	constexpr Vector3 kBridgeEye = { 0.0f, 12.2f, 9.6f };
	// 足場と鏡の床を見せるために見下ろす角度 [度]
	constexpr float kBridgePitchDegrees = 4.0f;
	// 待機の位置で止まるキーの時刻 [秒]
	constexpr float kHomeTime = 5.4f;
	// 待機の位置の目の位置（Start で待機の視点に合わせる）[m]
	constexpr Vector3 kHomeEye = { 0.0f, 12.2f, 6.6f };
	// 待機の位置の見上げる角度 [度]（− で上を向く）
	constexpr float kHomePitchDegrees = 2.0f;
	// 歩くときの頭の上下の幅 [m]
	constexpr float kBobAmplitude = 0.04f;
	// 歩くときの 1 秒あたりの歩数
	constexpr float kBobStepsPerSecond = 1.8f;
	// 待機へ切り替わる時刻の初期値 [秒]
	constexpr float kDefaultEndTime = 5.6f;
}

HubEntrySequence::HubEntrySequence() {
	ResetToDefaults();
}

void HubEntrySequence::ResetToDefaults() {
	using Ease = HubCameraTrack::Ease;
	// 機体の原点からの相対（正面 +Z）。yaw 180 = 機体の方（−Z）を向く。
	// 踊り場と足場は高さ 10.6m で、目はその 1.6m 上。時計の門の針は z 18.55 の面を回るので、その少し手前から
	track_.SetKeys({
		// 閉じたタイトルの扉のすぐ手前（時計の門の通り道）。少し見上げる。白フェードが晴れていく
		{ 0.0f, kDoorEye, kFaceMechYawDegrees, -kDoorPitchDegrees, Ease::Linear, false },
		// 踊り場を歩き出す
		{ kWalkStartTime, kWalkStartEye, kFaceMechYawDegrees, -kWalkStartPitchDegrees, Ease::In, true },
		// 足場の上を進む。少し見下ろして足場と鏡の床を見せる
		{ kBridgeTime, kBridgeEye, kFaceMechYawDegrees, kBridgePitchDegrees, Ease::Linear, true },
		// 待機の位置で止まる（位置と向きは Start で待機の視点に合わせる）
		{ kHomeTime, kHomeEye, kFaceMechYawDegrees, -kHomePitchDegrees, Ease::Out, true },
	});
	track_.SetBob(kBobAmplitude, kBobStepsPerSecond);
	endTime_ = kDefaultEndTime;
}

void HubEntrySequence::MatchHome(const HubCameraTrack::Key& home) {
	auto& keys = track_.GetKeys();
	if (keys.empty()) return;
	HubCameraTrack::Key& last = keys.back();
	last.eye = home.eye;
	last.yawDegrees = home.yawDegrees;
	last.pitchDegrees = home.pitchDegrees;
}

void HubEntrySequence::Start(const HubCameraTrack::Key& home) {
	MatchHome(home);
	time_ = 0.0f;
	playing_ = true;
	previewing_ = false;
}

void HubEntrySequence::Stop() {
	time_ = 0.0f;
	playing_ = false;
}

void HubEntrySequence::Update(float deltaTime) {
	if (!playing_) return;
	time_ += deltaTime;
}

HubEntrySequence::Pose HubEntrySequence::Evaluate(float time) const {
	const HubCameraTrack::Sample camera = track_.Evaluate(time);
	return { camera.eye, camera.yaw, camera.pitch };
}

void HubEntrySequence::LoadTuning(const JsonValue& node) {
	if (!node.IsObject()) return;
	endTime_ = static_cast<float>(node["endTime"].AsDouble(endTime_));
	track_.LoadTuning(node);
}

void HubEntrySequence::SaveTuning(JsonValue& node) const {
	node = JsonValue::MakeObject();
	node["endTime"] = JsonValue(static_cast<double>(endTime_));
	track_.SaveTuning(node);
}

void HubEntrySequence::OnImGui(const HubCameraTrack::Key& home) {
#ifdef _DEBUG
	ImGui::Text("Time: %.2f / %.2f s  %s", time_, endTime_, playing_ ? "(playing)" : "");
	if (ImGui::Checkbox("Preview (scrub)", &previewing_) && previewing_) {
		MatchHome(home);
	}
	ImGui::SameLine();
	ImGui::SetNextItemWidth(-1.0f);
	ImGui::SliderFloat("##EntryPreviewTime", &previewTime_, 0.0f, endTime_, "%.2f s");

	if (ImGui::Button("Reset To Defaults")) ResetToDefaults();
	ImGui::DragFloat("End (to waiting)", &endTime_, 0.01f, 0.0f, 30.0f);
	ImGui::TextDisabled("The last key's eye/yaw/pitch follow the waiting view (boarding key 0).");

	if (track_.OnImGui(previewTime_)) {
		previewing_ = true;
		MatchHome(home);
	}
#endif // _DEBUG
}
