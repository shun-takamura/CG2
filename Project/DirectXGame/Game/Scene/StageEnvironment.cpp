#include "StageEnvironment.h"

#include "Json/JsonValue.h"
#include "LightManager.h"
#include "MathUtility.h"
#include "Object3DManager.h"
#include "Skybox.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

// USE_IMGUI はこのプロジェクトでは未定義。RailStagePart / StagePlayScene と同じく直接 include する。
#include "imgui.h"

namespace {

	float Lerp1(float a, float b, float t) { return a + (b - a) * t; }

	Vector3 Lerp3(const Vector3& a, const Vector3& b, float t) {
		return { Lerp1(a.x, b.x, t), Lerp1(a.y, b.y, t), Lerp1(a.z, b.z, t) };
	}

	Vector4 Lerp4(const Vector4& a, const Vector4& b, float t) {
		return { Lerp1(a.x, b.x, t), Lerp1(a.y, b.y, t), Lerp1(a.z, b.z, t), Lerp1(a.w, b.w, t) };
	}

	StageEnvValues ToValues(const StageSection& s) {
		StageEnvValues v{};
		v.skyTint = s.skyboxTint;
		v.lightDir = s.lightDir;
		v.lightColor = s.lightColor;
		v.lightIntensity = s.lightIntensity;
		v.fogColor = s.fogColor;
		v.fogNear = s.fogNear;
		v.fogFar = s.fogFar;
		v.fogDensity = s.fogDensity;
		return v;
	}

	StageEnvValues BlendValues(const StageEnvValues& a, const StageEnvValues& b, float t) {
		StageEnvValues v{};
		v.skyTint = Lerp4(a.skyTint, b.skyTint, t);
		v.lightDir = Lerp3(a.lightDir, b.lightDir, t);
		v.lightColor = Lerp4(a.lightColor, b.lightColor, t);
		v.lightIntensity = Lerp1(a.lightIntensity, b.lightIntensity, t);
		v.fogColor = Lerp4(a.fogColor, b.fogColor, t);
		v.fogNear = Lerp1(a.fogNear, b.fogNear, t);
		v.fogFar = Lerp1(a.fogFar, b.fogFar, t);
		v.fogDensity = Lerp1(a.fogDensity, b.fogDensity, t);
		return v;
	}

	Vector3 ReadVec3(const JsonValue& j, const Vector3& fallback) {
		if (!j.IsArray() || j.Size() < 3) return fallback;
		return {
			static_cast<float>(j[0].AsDouble(fallback.x)),
			static_cast<float>(j[1].AsDouble(fallback.y)),
			static_cast<float>(j[2].AsDouble(fallback.z)),
		};
	}

	Vector4 ReadVec4(const JsonValue& j, const Vector4& fallback) {
		if (!j.IsArray() || j.Size() < 4) return fallback;
		return {
			static_cast<float>(j[0].AsDouble(fallback.x)),
			static_cast<float>(j[1].AsDouble(fallback.y)),
			static_cast<float>(j[2].AsDouble(fallback.z)),
			static_cast<float>(j[3].AsDouble(fallback.w)),
		};
	}

	JsonValue WriteVec3(const Vector3& v) {
		JsonValue a = JsonValue::MakeArray();
		a.Push(JsonValue(static_cast<double>(v.x)));
		a.Push(JsonValue(static_cast<double>(v.y)));
		a.Push(JsonValue(static_cast<double>(v.z)));
		return a;
	}

	JsonValue WriteVec4(const Vector4& v) {
		JsonValue a = JsonValue::MakeArray();
		a.Push(JsonValue(static_cast<double>(v.x)));
		a.Push(JsonValue(static_cast<double>(v.y)));
		a.Push(JsonValue(static_cast<double>(v.z)));
		a.Push(JsonValue(static_cast<double>(v.w)));
		return a;
	}

} // namespace

void StageEnvironment::Initialize(Skybox* skybox, Object3DManager* object3DManager,
	std::function<void(const std::string&)> onCubemapChanged) {
	skybox_ = skybox;
	object3DManager_ = object3DManager;
	onCubemapChanged_ = std::move(onCubemapChanged);
	skyIndex_ = -1;
}

int StageEnvironment::FindSectionIndex(float stageSec) const {
	if (sections_.empty()) return -1;
	int idx = 0;
	for (int i = 0; i < static_cast<int>(sections_.size()); ++i) {
		if (stageSec >= sections_[i].startSec) idx = i;
		else break;
	}
	return idx;
}

int StageEnvironment::FindSkyIndex(float stageSec) const {
	if (sections_.empty()) return -1;
	int idx = 0;
	for (int i = 0; i < static_cast<int>(sections_.size()); ++i) {
		// 遷移窓の先頭（startSec - blendSec/2）を跨いだ時点で「次の空」に移る。
		// こうすると Skybox の BlendTo と値の補間窓が中心を共有する。
		const float enter = sections_[i].startSec - sections_[i].blendSec * 0.5f;
		if (stageSec >= enter) idx = i;
		else break;
	}
	return idx;
}

const std::string* StageEnvironment::ResolveCubemapPath(int index) const {
	for (int i = index; i >= 0; --i) {
		if (!sections_[i].skyboxPath.empty()) return &sections_[i].skyboxPath;
	}
	return nullptr;
}

void StageEnvironment::Evaluate(float stageSec, StageEnvValues& out) const {
	const int n = static_cast<int>(sections_.size());
	const int i = FindSectionIndex(stageSec);
	if (i < 0) return;

	const StageEnvValues cur = ToValues(sections_[i]);

	// ①このセクションの「開始境界」の窓の中か（前のセクションから入ってきている途中）
	if (i > 0) {
		const float half = sections_[i].blendSec * 0.5f;
		if (half > 0.0f && stageSec < sections_[i].startSec + half) {
			const float u = (stageSec - (sections_[i].startSec - half)) / (half * 2.0f);
			out = BlendValues(ToValues(sections_[i - 1]), cur, std::clamp(u, 0.0f, 1.0f));
			return;
		}
	}

	// ②次のセクションの「開始境界」の窓に入っているか
	if (i + 1 < n) {
		const float half = sections_[i + 1].blendSec * 0.5f;
		if (half > 0.0f && stageSec > sections_[i + 1].startSec - half) {
			const float u = (stageSec - (sections_[i + 1].startSec - half)) / (half * 2.0f);
			out = BlendValues(cur, ToValues(sections_[i + 1]), std::clamp(u, 0.0f, 1.0f));
			return;
		}
	}

	// ③窓の外＝このセクションの素の値
	out = cur;
}

void StageEnvironment::ApplyValues(const StageEnvValues& v, bool applyTint) {
	if (skybox_ && applyTint) {
		skybox_->SetColor(v.skyTint);
	}

	auto* lm = LightManager::GetInstance();
	if (lm && lm->GetDirectionalLightData()) {
		Vector3 dir = v.lightDir;
		const float len = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
		if (len > 1e-6f) dir = { dir.x / len, dir.y / len, dir.z / len };
		lm->SetDirectionalLightDirection(dir);
		lm->SetDirectionalLightColor(v.lightColor);
		lm->SetDirectionalLightIntensity(v.lightIntensity);
	}

	// 距離フォグ（Object3D 系 PS の b6）。Primitive/パーティクル/Skybox は対象外。
	if (object3DManager_) {
		Object3DManager::FogParams fp{};
		fp.color = v.fogColor;
		fp.nearDist = v.fogNear;
		fp.farDist = v.fogFar;
		fp.density = v.fogDensity;
		fp.enabled = 1;
		object3DManager_->SetFogParams(fp);
	}
}

void StageEnvironment::Update(float stageSec, bool applyTint) {
	if (sections_.empty()) return;   // セクション未定義なら従来どおり何もしない

	const int sky = FindSkyIndex(stageSec);
	if (sky != skyIndex_) {
		const std::string* path = ResolveCubemapPath(sky);
		// 前方へ進んだ時だけクロスフェード。巻き戻し（F4/手動）は即差し替えにする。
		const bool forward = (sky > skyIndex_);
		if (path && skybox_ && *path != skybox_->GetCubemapFilePath()) {
			if (forward && sky >= 0 && sections_[sky].blendSec > 0.0f) {
				skybox_->BlendTo(*path, sections_[sky].blendSec);
			} else {
				skybox_->SetCubemap(*path);
			}
			if (onCubemapChanged_) onCubemapChanged_(*path);
		}
		skyIndex_ = sky;
	}

	Evaluate(stageSec, current_);
	ApplyValues(current_, applyTint);
}

void StageEnvironment::Reset(float stageSec) {
	if (sections_.empty()) return;

	const int sky = FindSkyIndex(stageSec);
	const std::string* path = ResolveCubemapPath(sky);
	if (path && skybox_ && *path != skybox_->GetCubemapFilePath()) {
		skybox_->SetCubemap(*path);   // Seek 中はクロスフェードしない（色の確認にならないため）
		if (onCubemapChanged_) onCubemapChanged_(*path);
	}
	skyIndex_ = sky;

	Evaluate(stageSec, current_);
	ApplyValues(current_, true);
}

void StageEnvironment::LoadFromJson(const JsonValue& root) {
	const JsonValue& arr = root["sections"];
	if (!arr.IsArray()) return;

	sections_.clear();
	for (size_t i = 0; i < arr.Size(); ++i) {
		const JsonValue& o = arr[i];
		if (!o.IsObject()) continue;

		StageSection s{};
		if (o["name"].IsString()) s.name = o["name"].AsString();
		s.startSec = static_cast<float>(o["startSec"].AsDouble(s.startSec));
		s.blendSec = static_cast<float>(o["blendSec"].AsDouble(s.blendSec));
		if (o["skyboxPath"].IsString()) s.skyboxPath = o["skyboxPath"].AsString();
		s.skyboxTint = ReadVec4(o["skyboxTint"], s.skyboxTint);

		s.lightDir = ReadVec3(o["lightDir"], s.lightDir);
		s.lightColor = ReadVec4(o["lightColor"], s.lightColor);
		s.lightIntensity = static_cast<float>(o["lightIntensity"].AsDouble(s.lightIntensity));

		s.fogColor = ReadVec4(o["fogColor"], s.fogColor);
		s.fogNear = static_cast<float>(o["fogNear"].AsDouble(s.fogNear));
		s.fogFar = static_cast<float>(o["fogFar"].AsDouble(s.fogFar));
		s.fogDensity = static_cast<float>(o["fogDensity"].AsDouble(s.fogDensity));

		sections_.push_back(std::move(s));
	}

	std::stable_sort(sections_.begin(), sections_.end(),
		[](const StageSection& a, const StageSection& b) { return a.startSec < b.startSec; });
	skyIndex_ = -1;
}

void StageEnvironment::SaveToJson(JsonValue& root) const {
	JsonValue arr = JsonValue::MakeArray();
	for (const auto& s : sections_) {
		JsonValue o = JsonValue::MakeObject();
		o["name"] = JsonValue(s.name);
		o["startSec"] = static_cast<double>(s.startSec);
		o["blendSec"] = static_cast<double>(s.blendSec);
		o["skyboxPath"] = JsonValue(s.skyboxPath);
		o["skyboxTint"] = WriteVec4(s.skyboxTint);

		o["lightDir"] = WriteVec3(s.lightDir);
		o["lightColor"] = WriteVec4(s.lightColor);
		o["lightIntensity"] = static_cast<double>(s.lightIntensity);

		o["fogColor"] = WriteVec4(s.fogColor);
		o["fogNear"] = static_cast<double>(s.fogNear);
		o["fogFar"] = static_cast<double>(s.fogFar);
		o["fogDensity"] = static_cast<double>(s.fogDensity);

		arr.Push(std::move(o));
	}
	root["sections"] = std::move(arr);
}

void StageEnvironment::OnImGuiTuning(bool& changed, const std::function<void(float)>& seekTo) {
	if (!ImGui::CollapsingHeader("Stage Sections", ImGuiTreeNodeFlags_DefaultOpen)) return;

	// 候補 Cubemap。StagePlayScene の "Skybox" セクションと同じ並び。
	static const char* kCubemaps[] = {
		"",   // = 前のセクションを引き継ぐ
		"Resources/Cubemaps/rogland_clear_night_8k.dds",
		"Resources/Cubemaps/rogland_clear_night_4k.dds",
		"Resources/Cubemaps/passendorf_snow_8k.dds",
	};

	ImGui::TextDisabled("道中の空/光/フォグを経過秒で切り替える。0件なら従来動作。");
	ImGui::Text("Now: sky=%d  tint=(%.2f %.2f %.2f)  lightI=%.2f",
		skyIndex_, current_.skyTint.x, current_.skyTint.y, current_.skyTint.z, current_.lightIntensity);

	if (ImGui::Button("Add Section")) {
		StageSection s{};
		s.startSec = sections_.empty() ? 0.0f : sections_.back().startSec + 60.0f;
		s.name = "Section" + std::to_string(sections_.size());
		sections_.push_back(std::move(s));
		selected_ = static_cast<int>(sections_.size()) - 1;
		skyIndex_ = -1;
		changed = true;
	}
	ImGui::SameLine();
	if (ImGui::Button("Remove Selected") && selected_ >= 0 && selected_ < static_cast<int>(sections_.size())) {
		sections_.erase(sections_.begin() + selected_);
		selected_ = -1;
		skyIndex_ = -1;
		changed = true;
	}

	// ----- 一覧 -----
	for (int i = 0; i < static_cast<int>(sections_.size()); ++i) {
		ImGui::PushID(i);
		char label[128];
		std::snprintf(label, sizeof(label), "%2d  %-14s  %6.1fs", i, sections_[i].name.c_str(), sections_[i].startSec);
		if (ImGui::Selectable(label, selected_ == i)) selected_ = i;
		if (seekTo) {
			ImGui::SameLine();
			if (ImGui::SmallButton("Jump")) seekTo(sections_[i].startSec);
		}
		ImGui::PopID();
	}

	// ----- 選択セクションの編集 -----
	if (selected_ < 0 || selected_ >= static_cast<int>(sections_.size())) {
		ImGui::TextDisabled("（一覧から選択すると編集できます）");
		return;
	}
	StageSection& s = sections_[selected_];
	ImGui::SeparatorText("Selected");

	// 入力中に毎フレーム詰め直すと編集が打ち消されるので、選択が変わった時だけ同期する。
	static char nameBuf[64] = {};
	static int  nameBufFor = -2;
	if (nameBufFor != selected_) {
		std::snprintf(nameBuf, sizeof(nameBuf), "%s", s.name.c_str());
		nameBufFor = selected_;
	}
	if (ImGui::InputText("Name", nameBuf, sizeof(nameBuf))) s.name = nameBuf;
	if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;

	if (ImGui::DragFloat("Start (s)", &s.startSec, 0.5f, 0.0f, 1800.0f, "%.1f")) {}
	if (ImGui::IsItemDeactivatedAfterEdit()) {
		std::stable_sort(sections_.begin(), sections_.end(),
			[](const StageSection& a, const StageSection& b) { return a.startSec < b.startSec; });
		selected_ = -1;
		skyIndex_ = -1;
		changed = true;
	}
	if (ImGui::DragFloat("Blend (s)", &s.blendSec, 0.1f, 0.0f, 30.0f, "%.1f")) {}
	if (ImGui::IsItemDeactivatedAfterEdit()) { skyIndex_ = -1; changed = true; }

	ImGui::SeparatorText("Sky");
	int sel = 0;
	for (int k = 0; k < IM_ARRAYSIZE(kCubemaps); ++k) {
		if (s.skyboxPath == kCubemaps[k]) { sel = k; break; }
	}
	if (ImGui::Combo("Cubemap", &sel, kCubemaps, IM_ARRAYSIZE(kCubemaps))) {
		s.skyboxPath = kCubemaps[sel];
		skyIndex_ = -1;   // 次の Update で貼り直させる
		changed = true;
	}
	ImGui::TextDisabled("（空欄 = 前のセクションの空を引き継ぐ）");
	ImGui::ColorEdit4("Tint", &s.skyboxTint.x);
	if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;

	ImGui::SeparatorText("Directional Light");
	ImGui::DragFloat3("Direction", &s.lightDir.x, 0.01f, -1.0f, 1.0f, "%.2f");
	if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
	ImGui::ColorEdit4("Light Color", &s.lightColor.x);
	if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
	ImGui::DragFloat("Intensity", &s.lightIntensity, 0.02f, 0.0f, 10.0f, "%.2f");
	if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;

	ImGui::SeparatorText("Fog（Phase B で接続。今は保存のみ）");
	ImGui::ColorEdit4("Fog Color", &s.fogColor.x);
	if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
	ImGui::DragFloat("Fog Near", &s.fogNear, 1.0f, 0.0f, 5000.0f, "%.0f");
	if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
	ImGui::DragFloat("Fog Far", &s.fogFar, 1.0f, 0.0f, 20000.0f, "%.0f");
	if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
	ImGui::DragFloat("Fog Density", &s.fogDensity, 0.01f, 0.0f, 4.0f, "%.2f");
	if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
}
