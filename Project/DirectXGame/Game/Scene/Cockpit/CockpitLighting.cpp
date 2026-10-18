#include "CockpitLighting.h"

#include "CockpitModel.h"
#include "Object3DInstance.h"
#include "ModelInstance.h"
#include "Material.h"
#include "LightManager.h"
#include "Json/JsonValue.h"
#include <algorithm>

#ifdef _DEBUG
#include "imgui.h"
#endif

namespace {
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

	// 部位の名前のどれかを含むか
	bool MatchesAny(const std::string& path, const std::vector<std::string>& names) {
		for (const std::string& name : names) {
			if (!name.empty() && path.find(name) != std::string::npos) return true;
		}
		return false;
	}
}

CockpitLighting::CockpitLighting() {
	lampSlots_.fill(kInvalidLightSlot);
	ResetToDefaults();
}

void CockpitLighting::ResetToDefaults() {
	// 操縦桿の緑の輪・操作盤のボタン（CP_Glow）、ペダルの光の帯（CP_Light）、壁のモニター（CP_Screen）
	glowParts_ = {
		{ "CP_Glow", { 0.25f, 1.0f, 0.45f }, 0.9f, true },
		{ "CP_Light", { 0.3f, 1.0f, 0.6f }, 0.5f, true },
		{ "CP_Screen", { 0.2f, 1.0f, 0.5f }, 0.25f, true },
	};
	// 外を映す：壁のモニター・枠・開口部の内枠・ハッチと下の板（HT_*）。座席・操縦桿・ペダルは残る
	monitorParts_ = { "CP_Screen", "CP_Frame", "BZ_Navy", "HT_" };
	// 足元の前（操作盤のあたり）と頭の上
	lamps_[0] = { { 0.0f, -0.3f, 0.6f }, 1.5f, 2.0f, 2.0f };
	lamps_[1] = { { 0.0f, 0.7f, 0.1f }, 0.8f, 2.5f, 2.0f };
	lampColor_ = { 0.3f, 1.0f, 0.55f };
	lampFollowSign_ = true;
	hexCellPixels_ = 42.0f;
	hexBand_ = 0.12f;
	hexJitter_ = 0.12f;
	hexColor_ = { 0.35f, 1.0f, 0.85f };
	bloomScale_ = 3.0f;
}

void CockpitLighting::Initialize() {
	auto* lm = LightManager::GetInstance();
	for (uint32_t& slot : lampSlots_) {
		slot = lm->AcquirePointLight();
	}
	level_ = 0.0f;
	reveal_ = 0.0f;
}

void CockpitLighting::Finalize(CockpitModel* cockpit) {
	auto* lm = LightManager::GetInstance();
	for (uint32_t& slot : lampSlots_) {
		if (slot != kInvalidLightSlot) lm->ReleasePointLight(slot);
		slot = kInvalidLightSlot;
	}
	// 共有のマテリアルを他のシーンへ持ち越さない
	if (!cockpit) return;
	for (Object3DInstance* part : { cockpit->GetCockpit(), cockpit->GetHatch() }) {
		ModelInstance* model = part ? part->GetModelInstance() : nullptr;
		if (!model) continue;
		model->ClearEmissive();
		for (const RenderSubmesh& sm : model->GetSubmeshes()) {
			if (sm.material) sm.material->dissolveEnable = kDissolveOff;
		}
	}
}

void CockpitLighting::Apply(CockpitModel& cockpit, float level, float reveal, const Vector2& screenSize,
	const Vector3& signColor) {
	level_ = (std::max)(level, 0.0f);
	reveal_ = std::clamp(reveal, 0.0f, 1.0f);

	// 緑のポイントライト
	auto* lm = LightManager::GetInstance();
	const Vector3& origin = cockpit.GetPosition();
	const Vector3 axis = CockpitModel::GetAxis();
	for (uint32_t i = 0; i < kLampCount; ++i) {
		const uint32_t slot = lampSlots_[i];
		if (slot == kInvalidLightSlot) continue;
		const Lamp& lamp = lamps_[i];
		const Vector3& lampColor = lampFollowSign_ ? signColor : lampColor_;
		lm->SetPointLightColor(slot, { lampColor.x, lampColor.y, lampColor.z, 1.0f });
		lm->SetPointLightPosition(slot, { origin.x + axis.x + lamp.offset.x, origin.y + axis.y + lamp.offset.y,
			origin.z + axis.z + lamp.offset.z });
		lm->SetPointLightIntensity(slot, lamp.intensity * level_);
		lm->SetPointLightRadius(slot, lamp.radius);
		lm->SetPointLightDecay(slot, lamp.decay);
	}

	// 六角形：画面の中心から、画面の高さに合わせた大きさで
	const float cellPixels = hexCellPixels_ * (std::max)(screenSize.y, 1.0f) / 900.0f;
	for (Object3DInstance* part : { cockpit.GetCockpit(), cockpit.GetHatch() }) {
		ModelInstance* model = part ? part->GetModelInstance() : nullptr;
		if (!model) continue;

		// 光る部位
		model->ClearEmissive();
		if (level_ > 0.0f) {
			for (const GlowPart& glow : glowParts_) {
				const float k = glow.strength * level_;
				const Vector3& c = glow.followSign ? signColor : glow.color;
				model->SetEmissiveByMaterialName(glow.material, { c.x * k, c.y * k, c.z * k });
			}
		}

		// 外を映す部位
		for (const RenderSubmesh& sm : model->GetSubmeshes()) {
			Material* m = sm.material;
			if (!m) continue;
			if (reveal_ <= 0.0f || !MatchesAny(sm.matFilePath, monitorParts_)) {
				m->dissolveEnable = kDissolveOff;
				continue;
			}
			m->dissolveEnable = kDissolveHexScreen;
			m->dissolveProgress = 1.0f - reveal_;
			m->dissolveNoiseScale = cellPixels;
			m->dissolveHeightMin = screenSize.x * 0.5f;
			m->dissolveHeightMax = screenSize.y * 0.5f;
			m->dissolveEdgeWidth = hexBand_;
			m->dissolveNoiseWeight = hexJitter_;
			m->dissolveEdgeColor = hexColor_;
		}
	}
}

void CockpitLighting::LoadTuning(const JsonValue& node) {
	if (!node.IsObject()) return;
	const JsonValue& glows = node["glowParts"];
	if (glows.IsArray()) {
		glowParts_.clear();
		for (size_t i = 0; i < glows.Size(); ++i) {
			const JsonValue& g = glows[i];
			GlowPart part;
			part.material = g["material"].AsString(std::string());
			part.color = ReadVector3(g["color"], { 1.0f, 1.0f, 1.0f });
			part.strength = static_cast<float>(g["strength"].AsDouble(1.0));
			part.followSign = g["followSign"].AsBool(true);
			glowParts_.push_back(part);
		}
	}
	const JsonValue& monitors = node["monitorParts"];
	if (monitors.IsArray()) {
		monitorParts_.clear();
		for (size_t i = 0; i < monitors.Size(); ++i) monitorParts_.push_back(monitors[i].AsString(std::string()));
	}
	const JsonValue& lamps = node["lamps"];
	if (lamps.IsArray()) {
		for (size_t i = 0; i < lamps.Size() && i < kLampCount; ++i) {
			const JsonValue& l = lamps[i];
			Lamp& lamp = lamps_[i];
			lamp.offset = ReadVector3(l["offset"], lamp.offset);
			lamp.intensity = static_cast<float>(l["intensity"].AsDouble(lamp.intensity));
			lamp.radius = static_cast<float>(l["radius"].AsDouble(lamp.radius));
			lamp.decay = static_cast<float>(l["decay"].AsDouble(lamp.decay));
		}
	}
	lampColor_ = ReadVector3(node["lampColor"], lampColor_);
	lampFollowSign_ = node["lampFollowSign"].AsBool(lampFollowSign_);
	hexCellPixels_ = static_cast<float>(node["hexCellPixels"].AsDouble(hexCellPixels_));
	hexBand_ = static_cast<float>(node["hexBand"].AsDouble(hexBand_));
	hexJitter_ = static_cast<float>(node["hexJitter"].AsDouble(hexJitter_));
	hexColor_ = ReadVector3(node["hexColor"], hexColor_);
	bloomScale_ = static_cast<float>(node["bloomScale"].AsDouble(bloomScale_));
}

void CockpitLighting::SaveTuning(JsonValue& node) const {
	node = JsonValue::MakeObject();
	JsonValue glows = JsonValue::MakeArray();
	for (const GlowPart& part : glowParts_) {
		JsonValue g = JsonValue::MakeObject();
		g["material"] = JsonValue(part.material);
		g["color"] = MakeVector3(part.color);
		g["strength"] = JsonValue(static_cast<double>(part.strength));
		g["followSign"] = JsonValue(part.followSign);
		glows.Push(std::move(g));
	}
	node["glowParts"] = std::move(glows);
	JsonValue monitors = JsonValue::MakeArray();
	for (const std::string& name : monitorParts_) monitors.Push(JsonValue(name));
	node["monitorParts"] = std::move(monitors);
	JsonValue lamps = JsonValue::MakeArray();
	for (const Lamp& lamp : lamps_) {
		JsonValue l = JsonValue::MakeObject();
		l["offset"] = MakeVector3(lamp.offset);
		l["intensity"] = JsonValue(static_cast<double>(lamp.intensity));
		l["radius"] = JsonValue(static_cast<double>(lamp.radius));
		l["decay"] = JsonValue(static_cast<double>(lamp.decay));
		lamps.Push(std::move(l));
	}
	node["lamps"] = std::move(lamps);
	node["lampColor"] = MakeVector3(lampColor_);
	node["lampFollowSign"] = JsonValue(lampFollowSign_);
	node["hexCellPixels"] = JsonValue(static_cast<double>(hexCellPixels_));
	node["hexBand"] = JsonValue(static_cast<double>(hexBand_));
	node["hexJitter"] = JsonValue(static_cast<double>(hexJitter_));
	node["hexColor"] = MakeVector3(hexColor_);
	node["bloomScale"] = JsonValue(static_cast<double>(bloomScale_));
}

void CockpitLighting::OnImGui() {
#ifdef _DEBUG
	ImGui::Text("Level %.2f  Reveal %.2f", level_, reveal_);
	if (ImGui::Button("Reset To Defaults")) ResetToDefaults();

	ImGui::SeparatorText("Glow Parts (material name in .mat path)");
	for (size_t i = 0; i < glowParts_.size(); ++i) {
		GlowPart& part = glowParts_[i];
		ImGui::PushID(static_cast<int>(i));
		ImGui::Text("%s", part.material.c_str());
		ImGui::Checkbox("Follow Sign Color", &part.followSign);
		if (!part.followSign) ImGui::ColorEdit3("Color", &part.color.x);
		ImGui::DragFloat("Strength", &part.strength, 0.01f, 0.0f, 10.0f);
		ImGui::PopID();
	}

	ImGui::SeparatorText("Lamps (from cockpit axis)");
	ImGui::Checkbox("Lamp Follows Sign Color", &lampFollowSign_);
	if (!lampFollowSign_) ImGui::ColorEdit3("Lamp Color", &lampColor_.x);
	for (uint32_t i = 0; i < kLampCount; ++i) {
		Lamp& lamp = lamps_[i];
		ImGui::PushID(1000 + static_cast<int>(i));
		ImGui::Text("Lamp %u", i);
		ImGui::DragFloat3("Offset", &lamp.offset.x, 0.01f);
		ImGui::DragFloat("Intensity", &lamp.intensity, 0.01f, 0.0f, 20.0f);
		ImGui::DragFloat("Radius", &lamp.radius, 0.01f, 0.1f, 20.0f);
		ImGui::DragFloat("Decay", &lamp.decay, 0.01f, 0.1f, 8.0f);
		ImGui::PopID();
	}

	ImGui::SeparatorText("Hex (outside view)");
	ImGui::DragFloat("Cell (px @900)", &hexCellPixels_, 0.5f, 8.0f, 200.0f);
	ImGui::DragFloat("Band", &hexBand_, 0.005f, 0.01f, 1.0f);
	ImGui::DragFloat("Jitter", &hexJitter_, 0.005f, 0.0f, 1.0f);
	ImGui::ColorEdit3("Hex Color", &hexColor_.x);
	ImGui::DragFloat("Bloom Scale", &bloomScale_, 0.05f, 0.0f, 20.0f);
#endif // _DEBUG
}
