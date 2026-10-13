#include "HubBoardingSequence.h"

#include "MathUtility.h"
#include "Json/JsonValue.h"
#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

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

	float ApplyEase(HubBoardingSequence::Ease ease, float t) {
		switch (ease) {
		case HubBoardingSequence::Ease::InOut: return SmoothStep01(t);
		case HubBoardingSequence::Ease::In:    return t * t;
		case HubBoardingSequence::Ease::Out:   return 1.0f - (1.0f - t) * (1.0f - t);
		default:                               return t;
		}
	}

	// Catmull-Rom。p1 と p2 が同じ（その場に留まる区間）ならそのまま返す
	// （前後のキーから付く接線で、座ったまま見上げる間に目がふらつくのを防ぐ）
	float CatmullRom(float p0, float p1, float p2, float p3, float t) {
		if (std::fabs(p2 - p1) < 1e-4f) return p1;
		const float t2 = t * t;
		const float t3 = t2 * t;
		return 0.5f * ((2.0f * p1) + (-p0 + p2) * t + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2
			+ (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3);
	}

	JsonValue MakeVector3(const Vector3& v) {
		JsonValue a = JsonValue::MakeArray();
		a.Push(JsonValue(static_cast<double>(v.x)));
		a.Push(JsonValue(static_cast<double>(v.y)));
		a.Push(JsonValue(static_cast<double>(v.z)));
		return a;
	}

	Vector3 ReadVector3(const JsonValue& a, const Vector3& fallback) {
		if (!a.IsArray() || a.Size() < 3) return fallback;
		return { static_cast<float>(a[0].AsDouble(fallback.x)), static_cast<float>(a[1].AsDouble(fallback.y)),
			static_cast<float>(a[2].AsDouble(fallback.z)) };
	}
}

HubBoardingSequence::HubBoardingSequence() {
	ResetToDefaults();
}

void HubBoardingSequence::ResetToDefaults() {
	// 機体の原点からの相対（正面 +Z）。回廊は高さ 10.6m（手のひらと同じ）で、目はその 1.6m 上。
	// 手のひらは (0, 10.57, 2.96) 付近、座った目は (0, 12.543, 0.985)（17 番 §5.1）
	keys_ = {
		// 待機：回廊から開いた胸を見上げる
		{ 0.0f, { 0.0f, 12.2f, 14.0f }, 180.0f, -6.0f, Ease::Linear, false },
		// 回廊の縁へ歩く。だんだん手のひらを見下ろす
		{ 1.8f, { 0.0f, 12.2f, 7.5f }, 180.0f, 14.0f, Ease::In, true },
		// 指先へ一段降りて手のひらに乗る
		{ 2.6f, { 0.0f, 12.1f, 5.6f }, 180.0f, 22.0f, Ease::Linear, true },
		// 手のひらを歩いて開口部の前へ
		{ 4.2f, { 0.0f, 12.2f, 3.4f }, 180.0f, 4.0f, Ease::Linear, true },
		// 開口部をくぐって中へ
		{ 5.0f, { 0.0f, 12.6f, 1.6f }, 180.0f, 0.0f, Ease::Out, true },
		// 振り返って座る
		{ 6.2f, { 0.0f, 12.543f, 0.985f }, 360.0f, 0.0f, Ease::InOut, false },
		// 降りてくるハッチを見上げる
		{ 7.0f, { 0.0f, 12.543f, 0.985f }, 360.0f, -50.0f, Ease::InOut, false },
		// 正面を向く。外の神殿が隙間に狭まっていく
		{ 8.6f, { 0.0f, 12.543f, 0.985f }, 360.0f, 0.0f, Ease::InOut, false },
	};
	sitTime_ = 6.2f;
	sitShakeIntensity_ = 0.025f;
	sitShakeSeconds_ = 0.25f;
	hatchStartTime_ = 6.6f;
	hatchCloseTime_ = 8.8f;
	hatchCloseExponent_ = 2.0f;
	closeShakeIntensity_ = 0.04f;
	closeShakeSeconds_ = 0.35f;
	lightDimStartRatio_ = 0.4f;
	minLightScale_ = 0.1f;
	blackoutStartTime_ = 8.55f;
	changeSceneTime_ = 9.1f;
	bobAmplitude_ = 0.04f;
	stepsPerSecond_ = 1.8f;
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
	if (keys_.empty()) return pose;

	// カメラ：キーを Catmull-Rom でつなぐ
	const int n = static_cast<int>(keys_.size());
	int seg = n - 1;
	for (int i = 0; i + 1 < n; ++i) {
		if (time < keys_[i + 1].time) { seg = i; break; }
	}
	if (time <= keys_.front().time || n == 1) {
		const CameraKey& k = keys_.front();
		pose.eye = k.eye;
		pose.yaw = DegToRad(k.yawDegrees);
		pose.pitch = DegToRad(k.pitchDegrees);
	} else if (seg >= n - 1) {
		const CameraKey& k = keys_.back();
		pose.eye = k.eye;
		pose.yaw = DegToRad(k.yawDegrees);
		pose.pitch = DegToRad(k.pitchDegrees);
	} else {
		const CameraKey& k0 = keys_[std::max(seg - 1, 0)];
		const CameraKey& k1 = keys_[seg];
		const CameraKey& k2 = keys_[seg + 1];
		const CameraKey& k3 = keys_[std::min(seg + 2, n - 1)];
		const float span = std::max(k2.time - k1.time, 1e-4f);
		const float u = ApplyEase(k2.ease, Clamp01((time - k1.time) / span));
		pose.eye = {
			CatmullRom(k0.eye.x, k1.eye.x, k2.eye.x, k3.eye.x, u),
			CatmullRom(k0.eye.y, k1.eye.y, k2.eye.y, k3.eye.y, u),
			CatmullRom(k0.eye.z, k1.eye.z, k2.eye.z, k3.eye.z, u),
		};
		pose.yaw = DegToRad(CatmullRom(k0.yawDegrees, k1.yawDegrees, k2.yawDegrees, k3.yawDegrees, u));
		pose.pitch = DegToRad(CatmullRom(k0.pitchDegrees, k1.pitchDegrees, k2.pitchDegrees, k3.pitchDegrees, u));

		// 歩く区間は頭が上下する。続けて歩く区間はひとまとまりにして、歩き出しと止まる所だけ弱める
		if (k2.walk && bobAmplitude_ > 0.0f) {
			int first = seg + 1;
			while (first - 1 > 0 && keys_[first - 1].walk) --first;
			int last = seg + 1;
			while (last + 1 < n && keys_[last + 1].walk) ++last;
			const float runStart = keys_[first - 1].time;
			const float runEnd = keys_[last].time;
			const float fade = 0.3f;
			const float envelope = SmoothStep01((time - runStart) / fade) * SmoothStep01((runEnd - time) / fade);
			const float phase = (time - runStart) * stepsPerSecond_ * 2.0f * kPi;
			pose.eye.y -= bobAmplitude_ * 0.5f * (1.0f - std::cos(phase)) * envelope;
		}
	}

	// ハッチ：ゆっくり降り始めて、最後に勢いよく閉まる
	const float hatchSpan = std::max(hatchCloseTime_ - hatchStartTime_, 1e-4f);
	const float closeProgress = Clamp01((time - hatchStartTime_) / hatchSpan);
	pose.hatchOpenRatio = 1.0f - std::pow(closeProgress, std::max(hatchCloseExponent_, 0.01f));

	// 外の光：隙間が狭まると弱まる
	const float dimStart = std::max(lightDimStartRatio_, 1e-4f);
	pose.lightScale = minLightScale_ + (1.0f - minLightScale_) * SmoothStep01(pose.hatchOpenRatio / dimStart);

	// 真っ暗：閉じ切りで 1
	const float blackSpan = std::max(hatchCloseTime_ - blackoutStartTime_, 1e-4f);
	pose.blackout = SmoothStep01((time - blackoutStartTime_) / blackSpan);
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
	readFloat("blackoutStartTime", blackoutStartTime_);
	readFloat("changeSceneTime", changeSceneTime_);
	readFloat("bobAmplitude", bobAmplitude_);
	readFloat("stepsPerSecond", stepsPerSecond_);

	const JsonValue& keys = node["keys"];
	if (keys.IsArray() && keys.Size() > 0) {
		keys_.clear();
		for (size_t i = 0; i < keys.Size(); ++i) {
			const JsonValue& k = keys[i];
			CameraKey key;
			key.time = static_cast<float>(k["time"].AsDouble(0.0));
			key.eye = ReadVector3(k["eye"], key.eye);
			key.yawDegrees = static_cast<float>(k["yaw"].AsDouble(0.0));
			key.pitchDegrees = static_cast<float>(k["pitch"].AsDouble(0.0));
			key.ease = static_cast<Ease>(std::clamp(static_cast<int>(k["ease"].AsInt(0)), 0, static_cast<int>(Ease::kCount) - 1));
			key.walk = k["walk"].AsBool(false);
			keys_.push_back(key);
		}
		std::stable_sort(keys_.begin(), keys_.end(),
			[](const CameraKey& a, const CameraKey& b) { return a.time < b.time; });
	}
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
	node["blackoutStartTime"] = JsonValue(static_cast<double>(blackoutStartTime_));
	node["changeSceneTime"] = JsonValue(static_cast<double>(changeSceneTime_));
	node["bobAmplitude"] = JsonValue(static_cast<double>(bobAmplitude_));
	node["stepsPerSecond"] = JsonValue(static_cast<double>(stepsPerSecond_));

	JsonValue keys = JsonValue::MakeArray();
	for (const CameraKey& key : keys_) {
		JsonValue k = JsonValue::MakeObject();
		k["time"] = JsonValue(static_cast<double>(key.time));
		k["eye"] = MakeVector3(key.eye);
		k["yaw"] = JsonValue(static_cast<double>(key.yawDegrees));
		k["pitch"] = JsonValue(static_cast<double>(key.pitchDegrees));
		k["ease"] = JsonValue(static_cast<int>(key.ease));
		k["walk"] = JsonValue(key.walk);
		keys.Push(std::move(k));
	}
	node["keys"] = std::move(keys);
}

void HubBoardingSequence::OnImGui() {
#ifdef _DEBUG
	ImGui::Text("Time: %.2f / %.2f s  %s", time_, changeSceneTime_, playing_ ? "(playing)" : "");
	ImGui::Checkbox("Preview (scrub)", &previewing_);
	ImGui::SameLine();
	ImGui::SetNextItemWidth(-1.0f);
	ImGui::SliderFloat("##PreviewTime", &previewTime_, 0.0f, changeSceneTime_, "%.2f s");

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
	ImGui::DragFloat("Blackout Start", &blackoutStartTime_, 0.01f, 0.0f, 30.0f);
	ImGui::DragFloat("Change Scene", &changeSceneTime_, 0.01f, 0.0f, 30.0f);
	ImGui::DragFloat("Bob Amplitude", &bobAmplitude_, 0.001f, 0.0f, 0.3f);
	ImGui::DragFloat("Steps / s", &stepsPerSecond_, 0.01f, 0.1f, 5.0f);

	ImGui::SeparatorText("Camera Keys (eye = from mech origin, pitch + = down)");
	static const char* kEaseNames[] = { "Linear", "InOut", "In", "Out" };
	bool timeEdited = false;
	int removeIndex = -1;
	int duplicateIndex = -1;
	for (int i = 0; i < static_cast<int>(keys_.size()); ++i) {
		CameraKey& key = keys_[i];
		ImGui::PushID(i);
		const std::string label = "Key " + std::to_string(i) + "  t=" + std::to_string(key.time).substr(0, 4);
		if (ImGui::TreeNode(label.c_str())) {
			timeEdited |= ImGui::DragFloat("Time", &key.time, 0.01f, 0.0f, 30.0f);
			ImGui::DragFloat3("Eye", &key.eye.x, 0.01f);
			ImGui::DragFloat("Yaw (deg)", &key.yawDegrees, 0.5f);
			ImGui::DragFloat("Pitch (deg)", &key.pitchDegrees, 0.5f, -89.0f, 89.0f);
			int ease = static_cast<int>(key.ease);
			if (ImGui::Combo("Ease (into)", &ease, kEaseNames, IM_ARRAYSIZE(kEaseNames))) key.ease = static_cast<Ease>(ease);
			ImGui::Checkbox("Walk (into)", &key.walk);
			if (ImGui::Button("Preview Here")) { previewing_ = true; previewTime_ = key.time; }
			ImGui::SameLine();
			if (ImGui::Button("Duplicate")) duplicateIndex = i;
			ImGui::SameLine();
			if (ImGui::Button("Remove") && keys_.size() > 1) removeIndex = i;
			ImGui::TreePop();
		}
		ImGui::PopID();
	}
	if (duplicateIndex >= 0) {
		CameraKey copy = keys_[duplicateIndex];
		copy.time += 0.5f;
		keys_.insert(keys_.begin() + duplicateIndex + 1, copy);
		timeEdited = true;
	}
	if (removeIndex >= 0) keys_.erase(keys_.begin() + removeIndex);
	if (timeEdited) {
		std::stable_sort(keys_.begin(), keys_.end(),
			[](const CameraKey& a, const CameraKey& b) { return a.time < b.time; });
	}
#endif // _DEBUG
}
