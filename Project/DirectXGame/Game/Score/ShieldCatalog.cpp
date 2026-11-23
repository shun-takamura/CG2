#include "ShieldCatalog.h"

#include "Json/JsonValue.h"
#include "Json/JsonVector.h"
#include "Json/JsonParser.h"
#include "Json/JsonWriter.h"
#include <cmath>
#include <filesystem>

namespace {
	constexpr const char* kTuningPath = "Resources/Json/Tuning/Shield.json";
	// 書き出す JSON の字下げの幅 [文字]
	constexpr int kJsonIndentWidth = 2;
	// 割合を百分率にする倍率
	constexpr float kPercentScale = 100.0f;
	constexpr const char* kComment = "20_Loadout.md §2 盾（左手）。maxHpAdd はプレハブの HP 上限に足す（飛行ユニットの補正の前）。"
		"damageCut は通常の被弾の軽減（0.2＝20%）、moveSpeedScale は移動速度の倍率。"
		"grip は左手に付けるモデルのずらし（model を空にすると描かない）。beam があればビームシールド（offset＝発生装置の位置、モデルのローカル）";

	// ビームの見た目の数値の欄（JSON のキーと BeamShieldAppearance のメンバ）
	struct BeamField { const char* key; float BeamShieldAppearance::* member; };
	constexpr BeamField kBeamFields[] = {
		{ "halfWidth", &BeamShieldAppearance::halfWidth }, { "halfHeight", &BeamShieldAppearance::halfHeight },
		{ "topY", &BeamShieldAppearance::topY }, { "shoulderY", &BeamShieldAppearance::shoulderY },
		{ "waistX", &BeamShieldAppearance::waistX }, { "waistY", &BeamShieldAppearance::waistY },
		{ "bottomY", &BeamShieldAppearance::bottomY },
		{ "edgeNoiseAmp", &BeamShieldAppearance::edgeNoiseAmp }, { "edgeNoiseFreq", &BeamShieldAppearance::edgeNoiseFreq },
		{ "edgeNoiseSpeed", &BeamShieldAppearance::edgeNoiseSpeed },
		{ "streakCount", &BeamShieldAppearance::streakCount }, { "streakSharpness", &BeamShieldAppearance::streakSharpness },
		{ "streakSpeed", &BeamShieldAppearance::streakSpeed }, { "streakIntensity", &BeamShieldAppearance::streakIntensity },
		{ "coreRadius", &BeamShieldAppearance::coreRadius }, { "coreAlpha", &BeamShieldAppearance::coreAlpha },
		{ "edgeWidth", &BeamShieldAppearance::edgeWidth }, { "edgeAlpha", &BeamShieldAppearance::edgeAlpha },
		{ "midAlpha", &BeamShieldAppearance::midAlpha },
		{ "innerScale", &BeamShieldAppearance::innerScale }, { "innerAlpha", &BeamShieldAppearance::innerAlpha },
		{ "intensity", &BeamShieldAppearance::intensity }, { "bloomScale", &BeamShieldAppearance::bloomScale },
		{ "flashIntensity", &BeamShieldAppearance::flashIntensity }, { "flashEdgeNoise", &BeamShieldAppearance::flashEdgeNoise },
		{ "flashDecay", &BeamShieldAppearance::flashDecay },
	};

	/// <summary>割合を符号付きの百分率の文字列にする（"+5%" / "-10%" / "±0%"）</summary>
	/// <param name="ratio">割合（0.05＝5%）</param>
	/// <returns>百分率の文字列</returns>
	std::string SignedPercent(float ratio) {
		const int pct = static_cast<int>(std::lround(ratio * kPercentScale));
		if (pct == 0) return "±0%";
		return (pct > 0 ? "+" : "") + std::to_string(pct) + "%";
	}
}

int ShieldDef::ReduceDamage(int damage) const {
	if (damage <= 0 || damageCut <= 0.0f) return damage;
	const int reduced = static_cast<int>(std::lround(static_cast<float>(damage) * (1.0f - damageCut)));
	return reduced < 1 ? 1 : reduced;
}

ShieldCatalog* ShieldCatalog::GetInstance() {
	static ShieldCatalog instance;
	return &instance;
}

ShieldCatalog::ShieldCatalog() {
	Load();
}

const ShieldDef* ShieldCatalog::GetDef(int index) const {
	if (index < 0 || index >= GetCount()) return nullptr;
	return &shields_[static_cast<size_t>(index)];
}

ShieldDef* ShieldCatalog::GetDefMutable(int index) {
	if (index < 0 || index >= GetCount()) return nullptr;
	return &shields_[static_cast<size_t>(index)];
}

const ShieldDef* ShieldCatalog::Find(const std::string& id) const {
	return GetDef(IndexOf(id));
}

int ShieldCatalog::IndexOf(const std::string& id) const {
	for (size_t i = 0; i < shields_.size(); ++i) {
		if (shields_[i].id == id) return static_cast<int>(i);
	}
	return -1;
}

std::vector<ShieldStatLine> ShieldCatalog::GetStatLines(const ShieldDef& def) {
	std::vector<ShieldStatLine> lines;
	lines.push_back({ "HP 上限", def.maxHpAdd == 0 ? "±0" : ((def.maxHpAdd > 0 ? "+" : "") + std::to_string(def.maxHpAdd)) });
	lines.push_back({ "被ダメージ軽減", std::to_string(static_cast<int>(std::lround(def.damageCut * kPercentScale))) + "%" });
	lines.push_back({ "移動速度", SignedPercent(def.moveSpeedScale - 1.0f) });
	return lines;
}

bool ShieldCatalog::Load() {
	if (!std::filesystem::exists(kTuningPath)) return false;
	const auto result = JsonParser::ParseFile(kTuningPath);
	if (!result.success) return false;
	const JsonValue& root = result.value;
	const JsonValue& list = root["shields"];
	if (!list.IsArray()) return false;

	std::vector<ShieldDef> shields;
	for (size_t i = 0; i < list.Size(); ++i) {
		const JsonValue& s = list[i];
		if (!s.IsObject()) continue;
		ShieldDef d;
		d.id = s["id"].AsString(std::string());
		if (d.id.empty()) continue;
		d.displayName = s["displayName"].AsString(d.id);
		d.description = s["description"].AsString(std::string());
		d.maxHpAdd = static_cast<int>(s["maxHpAdd"].AsInt(d.maxHpAdd));
		d.damageCut = static_cast<float>(s["damageCut"].AsDouble(d.damageCut));
		d.moveSpeedScale = static_cast<float>(s["moveSpeedScale"].AsDouble(d.moveSpeedScale));
		if (const JsonValue& m = s["model"]; m.IsObject()) {
			d.modelDir = m["dir"].AsString(std::string());
			d.modelFile = m["file"].AsString(std::string());
			d.bone = m["bone"].AsString(d.bone);
		}
		if (const JsonValue& g = s["grip"]; g.IsObject()) {
			d.gripTranslate = ReadVector3(g["translate"], d.gripTranslate);
			d.gripRotate = ReadVector3(g["rotate"], d.gripRotate);
			d.gripScale = ReadVector3(g["scale"], d.gripScale);
		}
		if (const JsonValue& b = s["beam"]; b.IsObject()) {
			d.beam = true;
			d.beamOffset = ReadVector3(b["offset"], d.beamOffset);
			for (const BeamField& f : kBeamFields) {
				d.beamLook.*f.member = static_cast<float>(b[f.key].AsDouble(d.beamLook.*f.member));
			}
			d.beamLook.color = ReadVector3(b["color"], d.beamLook.color);
			d.beamLook.edgeColor = ReadVector3(b["edgeColor"], d.beamLook.edgeColor);
		}
		shields.push_back(std::move(d));
	}
	shields_ = std::move(shields);

	defaultId_ = root["default"].AsString(std::string());
	if (IndexOf(defaultId_) < 0) defaultId_ = shields_.empty() ? std::string() : shields_.front().id;
	return true;
}

bool ShieldCatalog::Save() const {
	JsonValue root = JsonValue::MakeObject();
	root["_comment"] = JsonValue(std::string(kComment));
	root["default"] = JsonValue(defaultId_);
	JsonValue list = JsonValue::MakeArray();
	for (const ShieldDef& d : shields_) {
		JsonValue s = JsonValue::MakeObject();
		s["id"] = JsonValue(d.id);
		s["displayName"] = JsonValue(d.displayName);
		s["description"] = JsonValue(d.description);
		s["maxHpAdd"] = JsonValue(d.maxHpAdd);
		s["damageCut"] = JsonValue(static_cast<double>(d.damageCut));
		s["moveSpeedScale"] = JsonValue(static_cast<double>(d.moveSpeedScale));
		JsonValue m = JsonValue::MakeObject();
		m["dir"] = JsonValue(d.modelDir);
		m["file"] = JsonValue(d.modelFile);
		m["bone"] = JsonValue(d.bone);
		s["model"] = std::move(m);
		JsonValue g = JsonValue::MakeObject();
		g["translate"] = MakeVector3(d.gripTranslate);
		g["rotate"] = MakeVector3(d.gripRotate);
		g["scale"] = MakeVector3(d.gripScale);
		s["grip"] = std::move(g);
		if (d.beam) {
			JsonValue b = JsonValue::MakeObject();
			b["offset"] = MakeVector3(d.beamOffset);
			for (const BeamField& f : kBeamFields) b[f.key] = JsonValue(static_cast<double>(d.beamLook.*f.member));
			b["color"] = MakeVector3(d.beamLook.color);
			b["edgeColor"] = MakeVector3(d.beamLook.edgeColor);
			s["beam"] = std::move(b);
		}
		list.Push(std::move(s));
	}
	root["shields"] = std::move(list);
	return JsonWriter::WriteFile(kTuningPath, root, { true, kJsonIndentWidth });
}
