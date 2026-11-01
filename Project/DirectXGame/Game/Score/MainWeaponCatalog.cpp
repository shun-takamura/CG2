#include "MainWeaponCatalog.h"

#include "Json/JsonValue.h"
#include "Json/JsonParser.h"
#include <filesystem>

namespace {
	constexpr const char* kTuningPath = "Resources/Json/Tuning/MainWeapon.json";

	void ReadShot(const JsonValue& node, MainWeaponShot& s) {
		if (!node.IsObject()) return;
		s.speedScale = static_cast<float>(node["speedScale"].AsDouble(s.speedScale));
		s.sizeScale = static_cast<float>(node["sizeScale"].AsDouble(s.sizeScale));
		s.homingScale = static_cast<float>(node["homingScale"].AsDouble(s.homingScale));
	}

	void ReadVec3(const JsonValue& node, const char* key, Vector3& v) {
		const JsonValue& a = node[key];
		if (!a.IsArray() || a.Size() < 3) return;
		v = { static_cast<float>(a[0].AsDouble(v.x)), static_cast<float>(a[1].AsDouble(v.y)),
			static_cast<float>(a[2].AsDouble(v.z)) };
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
			ReadVec3(g, "translate", d.gripTranslate);
			ReadVec3(g, "rotate", d.gripRotate);
			ReadVec3(g, "scale", d.gripScale);
		}
		d.price = static_cast<int>(w["price"].AsInt(d.price));
		weapons.push_back(std::move(d));
	}
	weapons_ = std::move(weapons);

	defaultId_ = root["default"].AsString(std::string());
	if (IndexOf(defaultId_) < 0) defaultId_ = weapons_.empty() ? std::string() : weapons_.front().id;
	return true;
}
