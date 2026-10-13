#include "HubEntrySequence.h"

#include "Json/JsonValue.h"

#ifdef _DEBUG
#include "imgui.h"
#endif

HubEntrySequence::HubEntrySequence() {
	ResetToDefaults();
}

void HubEntrySequence::ResetToDefaults() {
	using Ease = HubCameraTrack::Ease;
	// 機体の原点からの相対（正面 +Z）。yaw 180 = 機体の方（−Z）を向く。
	// 踊り場と足場は高さ 10.6m で、目はその 1.6m 上。時計の門の針は z 18.55 の面を回るので、その少し手前から
	track_.SetKeys({
		// 閉じたタイトルの扉のすぐ手前（時計の門の通り道）。少し見上げる。白フェードが晴れていく
		{ 0.0f, { 0.0f, 12.2f, 17.8f }, 180.0f, -6.0f, Ease::Linear, false },
		// 踊り場を歩き出す
		{ 1.6f, { 0.0f, 12.2f, 15.2f }, 180.0f, -3.0f, Ease::In, true },
		// 足場の上を進む。少し見下ろして足場と鏡の床を見せる
		{ 4.0f, { 0.0f, 12.2f, 9.6f }, 180.0f, 4.0f, Ease::Linear, true },
		// 待機の位置で止まる（位置と向きは Start で待機の視点に合わせる）
		{ 5.4f, { 0.0f, 12.2f, 6.6f }, 180.0f, -2.0f, Ease::Out, true },
	});
	track_.SetBob(0.04f, 1.8f);
	endTime_ = 5.6f;
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
