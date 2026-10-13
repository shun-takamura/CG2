#include "HubCameraTrack.h"

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

	float ApplyEase(HubCameraTrack::Ease ease, float t) {
		switch (ease) {
		case HubCameraTrack::Ease::InOut: return SmoothStep01(t);
		case HubCameraTrack::Ease::In:    return t * t;
		case HubCameraTrack::Ease::Out:   return 1.0f - (1.0f - t) * (1.0f - t);
		default:                          return t;
		}
	}

	// Catmull-Rom。p1 と p2 が同じ（その場に留まる区間）ならそのまま返す
	// （前後のキーから付く接線で、座ったままの間に目がふらつくのを防ぐ）
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

void HubCameraTrack::SetKeys(std::vector<Key> keys) {
	keys_ = std::move(keys);
	SortKeys();
}

void HubCameraTrack::SortKeys() {
	std::stable_sort(keys_.begin(), keys_.end(), [](const Key& a, const Key& b) { return a.time < b.time; });
}

HubCameraTrack::Sample HubCameraTrack::Evaluate(float time) const {
	Sample s{};
	if (keys_.empty()) return s;

	const int n = static_cast<int>(keys_.size());
	int seg = n - 1;
	for (int i = 0; i + 1 < n; ++i) {
		if (time < keys_[i + 1].time) { seg = i; break; }
	}
	if (time <= keys_.front().time || n == 1) {
		const Key& k = keys_.front();
		s.eye = k.eye;
		s.yaw = DegToRad(k.yawDegrees);
		s.pitch = DegToRad(k.pitchDegrees);
		return s;
	}
	if (seg >= n - 1) {
		const Key& k = keys_.back();
		s.eye = k.eye;
		s.yaw = DegToRad(k.yawDegrees);
		s.pitch = DegToRad(k.pitchDegrees);
		return s;
	}

	const Key& k0 = keys_[(std::max)(seg - 1, 0)];
	const Key& k1 = keys_[seg];
	const Key& k2 = keys_[seg + 1];
	const Key& k3 = keys_[(std::min)(seg + 2, n - 1)];
	const float span = (std::max)(k2.time - k1.time, 1e-4f);
	const float u = ApplyEase(k2.ease, Clamp01((time - k1.time) / span));
	s.eye = {
		CatmullRom(k0.eye.x, k1.eye.x, k2.eye.x, k3.eye.x, u),
		CatmullRom(k0.eye.y, k1.eye.y, k2.eye.y, k3.eye.y, u),
		CatmullRom(k0.eye.z, k1.eye.z, k2.eye.z, k3.eye.z, u),
	};
	s.yaw = DegToRad(CatmullRom(k0.yawDegrees, k1.yawDegrees, k2.yawDegrees, k3.yawDegrees, u));
	s.pitch = DegToRad(CatmullRom(k0.pitchDegrees, k1.pitchDegrees, k2.pitchDegrees, k3.pitchDegrees, u));

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
		s.eye.y -= bobAmplitude_ * 0.5f * (1.0f - std::cos(phase)) * envelope;
	}
	return s;
}

void HubCameraTrack::LoadTuning(const JsonValue& node) {
	if (!node.IsObject()) return;
	bobAmplitude_ = static_cast<float>(node["bobAmplitude"].AsDouble(bobAmplitude_));
	stepsPerSecond_ = static_cast<float>(node["stepsPerSecond"].AsDouble(stepsPerSecond_));

	const JsonValue& keys = node["keys"];
	if (keys.IsArray() && keys.Size() > 0) {
		keys_.clear();
		for (size_t i = 0; i < keys.Size(); ++i) {
			const JsonValue& k = keys[i];
			Key key;
			key.time = static_cast<float>(k["time"].AsDouble(0.0));
			key.eye = ReadVector3(k["eye"], key.eye);
			key.yawDegrees = static_cast<float>(k["yaw"].AsDouble(0.0));
			key.pitchDegrees = static_cast<float>(k["pitch"].AsDouble(0.0));
			key.ease = static_cast<Ease>(std::clamp(static_cast<int>(k["ease"].AsInt(0)), 0, static_cast<int>(Ease::kCount) - 1));
			key.walk = k["walk"].AsBool(false);
			keys_.push_back(key);
		}
		SortKeys();
	}
}

void HubCameraTrack::SaveTuning(JsonValue& node) const {
	node["bobAmplitude"] = JsonValue(static_cast<double>(bobAmplitude_));
	node["stepsPerSecond"] = JsonValue(static_cast<double>(stepsPerSecond_));
	JsonValue keys = JsonValue::MakeArray();
	for (const Key& key : keys_) {
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

bool HubCameraTrack::OnImGui(float& previewTime) {
	bool previewRequested = false;
#ifdef _DEBUG
	ImGui::DragFloat("Bob Amplitude", &bobAmplitude_, 0.001f, 0.0f, 0.3f);
	ImGui::DragFloat("Steps / s", &stepsPerSecond_, 0.01f, 0.1f, 5.0f);

	ImGui::SeparatorText("Camera Keys (eye = from mech origin, pitch + = down)");
	static const char* kEaseNames[] = { "Linear", "InOut", "In", "Out" };
	bool timeEdited = false;
	int removeIndex = -1;
	int duplicateIndex = -1;
	for (int i = 0; i < static_cast<int>(keys_.size()); ++i) {
		Key& key = keys_[i];
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
			if (ImGui::Button("Preview Here")) {
				previewTime = key.time;
				previewRequested = true;
			}
			ImGui::SameLine();
			if (ImGui::Button("Duplicate")) duplicateIndex = i;
			ImGui::SameLine();
			if (ImGui::Button("Remove") && keys_.size() > 1) removeIndex = i;
			ImGui::TreePop();
		}
		ImGui::PopID();
	}
	if (duplicateIndex >= 0) {
		Key copy = keys_[duplicateIndex];
		copy.time += 0.5f;
		keys_.insert(keys_.begin() + duplicateIndex + 1, copy);
		timeEdited = true;
	}
	if (removeIndex >= 0) keys_.erase(keys_.begin() + removeIndex);
	if (timeEdited) SortKeys();
#endif // _DEBUG
	return previewRequested;
}
