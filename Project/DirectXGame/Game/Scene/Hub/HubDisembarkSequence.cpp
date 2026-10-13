#include "HubDisembarkSequence.h"

#include "Json/JsonValue.h"
#include <algorithm>

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

HubDisembarkSequence::HubDisembarkSequence() {
	ResetToDefaults();
}

void HubDisembarkSequence::ResetToDefaults() {
	using Ease = HubCameraTrack::Ease;
	// 機体の原点からの相対（正面 +Z）。座った目は外（+Z）を向いているので、そのまま前へ歩けば機体に背を向けて出られる
	track_.SetKeys({
		// 座ったまま正面を向いてハッチが開くのを待つ（見上げない）
		{ 0.0f, { 0.0f, 12.543f, 0.985f }, 360.0f, 0.0f, Ease::Linear, false },
		{ 2.2f, { 0.0f, 12.543f, 0.985f }, 360.0f, 0.0f, Ease::Linear, false },
		// 立ち上がる
		{ 2.7f, { 0.0f, 12.75f, 1.25f }, 360.0f, 5.0f, Ease::InOut, false },
		// 開口部をくぐって外へ
		{ 3.5f, { 0.0f, 12.6f, 2.0f }, 360.0f, 8.0f, Ease::Linear, true },
		// 手のひらを歩く（足元を見る）
		{ 4.9f, { 0.0f, 12.2f, 2.7f }, 360.0f, 14.0f, Ease::Linear, true },
		// 手のひらの先から足場へ渡る（足場の先は z 3.85。手のいちばん前は z 3.69）
		{ 5.7f, { 0.0f, 12.2f, 3.9f }, 360.0f, 12.0f, Ease::Linear, true },
		// 足場の上を歩く
		{ 6.4f, { 0.0f, 12.2f, 5.2f }, 360.0f, 2.0f, Ease::Linear, true },
		// 足場の上の待機の位置まで（位置は Start で待機の視点に合わせる）
		{ 7.0f, { 0.0f, 12.2f, 6.6f }, 360.0f, 0.0f, Ease::Out, true },
		// 振り返って機体を見る（位置と向きは Start で待機の視点に合わせる）
		{ 8.2f, { 0.0f, 12.2f, 6.6f }, 540.0f, -2.0f, Ease::InOut, false },
	});
	track_.SetBob(0.04f, 1.8f);
	hatchStartTime_ = 0.2f;
	hatchOpenTime_ = 2.0f;
	lightDimStartRatio_ = 0.4f;
	endTime_ = 8.3f;
}

void HubDisembarkSequence::MatchHome(const HubCameraTrack::Key& home) {
	auto& keys = track_.GetKeys();
	if (keys.size() < 2) return;
	HubCameraTrack::Key& walkEnd = keys[keys.size() - 2];
	HubCameraTrack::Key& turn = keys.back();
	walkEnd.eye = home.eye;
	turn.eye = home.eye;
	// 待機の向きを、歩き終わりの向きより先（巻き戻さずに回す側）の同じ向きにそろえる
	float target = home.yawDegrees;
	while (target <= walkEnd.yawDegrees) target += 360.0f;
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
	const float span = (std::max)(hatchOpenTime_ - hatchStartTime_, 1e-4f);
	pose.hatchOpenRatio = SmoothStep01((time - hatchStartTime_) / span);

	// 外の光：隙間が広がると戻る（乗り込みと同じ式）
	const float dimStart = (std::max)(lightDimStartRatio_, 1e-4f);
	pose.lightScale = minLightScale_ + (1.0f - minLightScale_) * SmoothStep01(pose.hatchOpenRatio / dimStart);
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
	readFloat("endTime", endTime_);
	track_.LoadTuning(node);
}

void HubDisembarkSequence::SaveTuning(JsonValue& node) const {
	node = JsonValue::MakeObject();
	node["hatchStartTime"] = JsonValue(static_cast<double>(hatchStartTime_));
	node["hatchOpenTime"] = JsonValue(static_cast<double>(hatchOpenTime_));
	node["lightDimStartRatio"] = JsonValue(static_cast<double>(lightDimStartRatio_));
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
	ImGui::DragFloat("End (to waiting)", &endTime_, 0.01f, 0.0f, 30.0f);
	ImGui::TextDisabled("Last 2 keys' eye and the last key's yaw/pitch follow the waiting view (boarding key 0).");

	if (track_.OnImGui(previewTime_)) {
		previewing_ = true;
		MatchHome(home);
		minLightScale_ = minLightScale;
	}
#endif // _DEBUG
}
