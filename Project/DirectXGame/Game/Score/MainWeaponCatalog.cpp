#include "MainWeaponCatalog.h"

#include "Json/JsonValue.h"
#include "Json/JsonVector.h"
#include "Json/JsonParser.h"
#include "Json/JsonWriter.h"
#include <filesystem>

namespace {
	constexpr const char* kTuningPath = "Resources/Json/Tuning/MainWeapon.json";
	// 書き出す JSON の字下げの幅 [文字]
	constexpr int kJsonIndentWidth = 2;
	constexpr const char* kComment = "20_Loadout.md §1 メイン武器（右手）。attackBonus はプレハブの攻撃力に足す（射撃・近接で分けない）。秒は実時間。"
		"normal＝通常弾、charge＝チャージ弾（1・2 段目とも）の倍率。sizeScale は弾の当たり判定と見た目の両方。"
		"grip は右手に付けるモデルのずらし（model を空にするとプレハブの weapon のまま）。"
		"blade は近接のときだけ出すビームの刃（offset / rotate / scale はモデルのローカル、銃口が +Z。刃は +Z へ伸び、幅は Y）。"
		"price は購入を入れるときのための予約（今は使わない）";

	// 刃の見た目の数値の欄（JSON のキーと BeamBladeAppearance のメンバ）
	struct BladeField { const char* key; float BeamBladeAppearance::* member; };
	constexpr BladeField kBladeFields[] = {
		{ "length", &BeamBladeAppearance::length }, { "halfWidth", &BeamBladeAppearance::halfWidth },
		{ "halfThickness", &BeamBladeAppearance::halfThickness }, { "baseWidth", &BeamBladeAppearance::baseWidth },
		{ "tipLength", &BeamBladeAppearance::tipLength }, { "tipOffset", &BeamBladeAppearance::tipOffset },
		{ "ridgeOffset", &BeamBladeAppearance::ridgeOffset }, { "backAnchor", &BeamBladeAppearance::backAnchor },
		{ "intensity", &BeamBladeAppearance::intensity }, { "bodyAlpha", &BeamBladeAppearance::bodyAlpha },
		{ "edgeAlpha", &BeamBladeAppearance::edgeAlpha }, { "edgeSharpness", &BeamBladeAppearance::edgeSharpness },
		{ "fresnelPower", &BeamBladeAppearance::fresnelPower }, { "fresnelStrength", &BeamBladeAppearance::fresnelStrength },
		{ "bloomScale", &BeamBladeAppearance::bloomScale },
		{ "extendTime", &BeamBladeAppearance::extendTime }, { "retractTime", &BeamBladeAppearance::retractTime },
	};

	/// <summary>弾 1 種類の倍率を読む。オブジェクトでなければ何もしない</summary>
	/// <param name="node">読む JSON オブジェクト</param>
	/// <param name="s">書き込み先（無いキーは元の値のまま）</param>
	void ReadShot(const JsonValue& node, MainWeaponShot& s) {
		if (!node.IsObject()) return;
		s.speedScale = static_cast<float>(node["speedScale"].AsDouble(s.speedScale));
		s.sizeScale = static_cast<float>(node["sizeScale"].AsDouble(s.sizeScale));
		s.homingScale = static_cast<float>(node["homingScale"].AsDouble(s.homingScale));
	}

	/// <summary>弾 1 種類の倍率を JSON にする</summary>
	/// <param name="s">書き出す倍率</param>
	/// <returns>JSON オブジェクト</returns>
	JsonValue MakeShot(const MainWeaponShot& s) {
		JsonValue o = JsonValue::MakeObject();
		o["speedScale"] = JsonValue(static_cast<double>(s.speedScale));
		o["sizeScale"] = JsonValue(static_cast<double>(s.sizeScale));
		o["homingScale"] = JsonValue(static_cast<double>(s.homingScale));
		return o;
	}
}

MainWeaponCatalog* MainWeaponCatalog::GetInstance() {
	static MainWeaponCatalog instance;
	return &instance;
}

MainWeaponCatalog::MainWeaponCatalog() {
	Load();
}

const MainWeaponDef* MainWeaponCatalog::GetDef(int index) const {
	if (index < 0 || index >= GetCount()) return nullptr;
	return &weapons_[static_cast<size_t>(index)];
}

MainWeaponDef* MainWeaponCatalog::GetDefMutable(int index) {
	if (index < 0 || index >= GetCount()) return nullptr;
	return &weapons_[static_cast<size_t>(index)];
}

const MainWeaponDef* MainWeaponCatalog::Find(const std::string& id) const {
	return GetDef(IndexOf(id));
}

int MainWeaponCatalog::IndexOf(const std::string& id) const {
	for (size_t i = 0; i < weapons_.size(); ++i) {
		if (weapons_[i].id == id) return static_cast<int>(i);
	}
	return -1;
}

bool MainWeaponCatalog::Load() {
	if (!std::filesystem::exists(kTuningPath)) return false;
	const auto result = JsonParser::ParseFile(kTuningPath);
	if (!result.success) return false;
	const JsonValue& root = result.value;
	const JsonValue& list = root["weapons"];
	if (!list.IsArray()) return false;

	std::vector<MainWeaponDef> weapons;
	for (size_t i = 0; i < list.Size(); ++i) {
		const JsonValue& w = list[i];
		if (!w.IsObject()) continue;
		MainWeaponDef d;
		d.id = w["id"].AsString(std::string());
		if (d.id.empty()) continue;
		d.displayName = w["displayName"].AsString(d.id);
		d.description = w["description"].AsString(std::string());
		d.attackBonus = static_cast<int>(w["attackBonus"].AsInt(d.attackBonus));
		d.fireRate = static_cast<float>(w["fireRate"].AsDouble(d.fireRate));
		d.chargeStage1Time = static_cast<float>(w["chargeStage1Time"].AsDouble(d.chargeStage1Time));
		d.chargeStage2Time = static_cast<float>(w["chargeStage2Time"].AsDouble(d.chargeStage2Time));
		ReadShot(w["normal"], d.normal);
		ReadShot(w["charge"], d.charge);
		if (const JsonValue& m = w["model"]; m.IsObject()) {
			d.modelDir = m["dir"].AsString(std::string());
			d.modelFile = m["file"].AsString(std::string());
		}
		if (const JsonValue& g = w["grip"]; g.IsObject()) {
			d.gripTranslate = ReadVector3(g["translate"], d.gripTranslate);
			d.gripRotate = ReadVector3(g["rotate"], d.gripRotate);
			d.gripScale = ReadVector3(g["scale"], d.gripScale);
		}
		if (const JsonValue& b = w["blade"]; b.IsObject()) {
			d.blade = true;
			d.bladeOffset = ReadVector3(b["offset"], d.bladeOffset);
			d.bladeRotate = ReadVector3(b["rotate"], d.bladeRotate);
			d.bladeScale = ReadVector3(b["scale"], d.bladeScale);
			for (const BladeField& f : kBladeFields) {
				d.bladeLook.*f.member = static_cast<float>(b[f.key].AsDouble(d.bladeLook.*f.member));
			}
			d.bladeLook.coreColor = ReadVector3(b["coreColor"], d.bladeLook.coreColor);
			d.bladeLook.edgeColor = ReadVector3(b["edgeColor"], d.bladeLook.edgeColor);
		}
		d.price = static_cast<int>(w["price"].AsInt(d.price));
		weapons.push_back(std::move(d));
	}
	weapons_ = std::move(weapons);

	defaultId_ = root["default"].AsString(std::string());
	if (IndexOf(defaultId_) < 0) defaultId_ = weapons_.empty() ? std::string() : weapons_.front().id;
	return true;
}

bool MainWeaponCatalog::Save() const {
	JsonValue root = JsonValue::MakeObject();
	root["_comment"] = JsonValue(std::string(kComment));
	root["default"] = JsonValue(defaultId_);
	JsonValue list = JsonValue::MakeArray();
	for (const MainWeaponDef& d : weapons_) {
		JsonValue w = JsonValue::MakeObject();
		w["id"] = JsonValue(d.id);
		w["displayName"] = JsonValue(d.displayName);
		w["description"] = JsonValue(d.description);
		w["attackBonus"] = JsonValue(d.attackBonus);
		w["fireRate"] = JsonValue(static_cast<double>(d.fireRate));
		w["chargeStage1Time"] = JsonValue(static_cast<double>(d.chargeStage1Time));
		w["chargeStage2Time"] = JsonValue(static_cast<double>(d.chargeStage2Time));
		w["normal"] = MakeShot(d.normal);
		w["charge"] = MakeShot(d.charge);
		JsonValue m = JsonValue::MakeObject();
		m["dir"] = JsonValue(d.modelDir);
		m["file"] = JsonValue(d.modelFile);
		w["model"] = std::move(m);
		JsonValue g = JsonValue::MakeObject();
		g["translate"] = MakeVector3(d.gripTranslate);
		g["rotate"] = MakeVector3(d.gripRotate);
		g["scale"] = MakeVector3(d.gripScale);
		w["grip"] = std::move(g);
		if (d.blade) {
			JsonValue b = JsonValue::MakeObject();
			b["offset"] = MakeVector3(d.bladeOffset);
			b["rotate"] = MakeVector3(d.bladeRotate);
			b["scale"] = MakeVector3(d.bladeScale);
			for (const BladeField& f : kBladeFields) b[f.key] = JsonValue(static_cast<double>(d.bladeLook.*f.member));
			b["coreColor"] = MakeVector3(d.bladeLook.coreColor);
			b["edgeColor"] = MakeVector3(d.bladeLook.edgeColor);
			w["blade"] = std::move(b);
		}
		w["price"] = JsonValue(d.price);
		list.Push(std::move(w));
	}
	root["weapons"] = std::move(list);
	return JsonWriter::WriteFile(kTuningPath, root, { true, kJsonIndentWidth });
}
