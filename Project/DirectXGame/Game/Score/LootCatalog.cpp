#include "LootCatalog.h"

#include "Json/JsonValue.h"
#include "Json/JsonParser.h"
#include <filesystem>

namespace {
	constexpr const char* kTuningPath = "Resources/Json/Tuning/Loot.json";

	Vector4 ReadVec4(const JsonValue& a, const Vector4& fallback) {
		if (!a.IsArray() || a.Size() < 4) return fallback;
		return { static_cast<float>(a[0].AsDouble(fallback.x)), static_cast<float>(a[1].AsDouble(fallback.y)),
			static_cast<float>(a[2].AsDouble(fallback.z)), static_cast<float>(a[3].AsDouble(fallback.w)) };
	}

	LootItemDef ReadItem(const JsonValue& node, const LootItemDef& fallback) {
		LootItemDef d = fallback;
		if (!node.IsObject()) return d;
		d.id = node["id"].AsString(d.id);
		d.displayName = node["displayName"].AsString(d.id);
		d.prefab = node["prefab"].AsString(d.prefab);
		d.color = ReadVec4(node["color"], d.color);
		d.scale = static_cast<float>(node["scale"].AsDouble(d.scale));
		d.spinSpeed = static_cast<float>(node["spinSpeed"].AsDouble(d.spinSpeed));
		return d;
	}

	LootTable ReadTable(const JsonValue& node) {
		LootTable t;
		if (!node.IsObject()) return t;
		t.prefab = node["prefab"].AsString(std::string());
		t.ether = static_cast<int>(node["ether"].AsInt(0));
		const JsonValue& rolls = node["items"];
		if (rolls.IsArray()) {
			for (size_t i = 0; i < rolls.Size(); ++i) {
				const JsonValue& r = rolls[i];
				if (!r.IsObject()) continue;
				LootRoll roll;
				roll.itemId = r["id"].AsString(std::string());
				if (roll.itemId.empty()) continue;
				roll.chance = static_cast<float>(r["chance"].AsDouble(roll.chance));
				roll.count = static_cast<int>(r["count"].AsInt(roll.count));
				t.rolls.push_back(roll);
			}
		}
		return t;
	}
}

LootCatalog* LootCatalog::GetInstance() {
	static LootCatalog instance;
	return &instance;
}

LootCatalog::LootCatalog() {
	ether_.id = "Ether";
	ether_.displayName = "エーテル";
	ether_.color = { 0.0f, 0.641f, 1.0f, 1.0f };
	Load();
}

const LootItemDef* LootCatalog::GetItem(int index) const {
	if (index < 0 || index >= GetItemCount()) return nullptr;
	return &items_[static_cast<size_t>(index)];
}

const LootItemDef* LootCatalog::FindItem(const std::string& id) const {
	return GetItem(IndexOf(id));
}

int LootCatalog::IndexOf(const std::string& id) const {
	for (size_t i = 0; i < items_.size(); ++i) {
		if (items_[i].id == id) return static_cast<int>(i);
	}
	return -1;
}

const LootTable* LootCatalog::FindTable(const std::string& prefab) const {
	for (const LootTable& t : tables_) {
		if (t.prefab == prefab) return &t;
	}
	return nullptr;
}

bool LootCatalog::Load() {
	if (!std::filesystem::exists(kTuningPath)) return false;
	const auto result = JsonParser::ParseFile(kTuningPath);
	if (!result.success) return false;
	const JsonValue& root = result.value;

	std::vector<LootItemDef> items;
	const JsonValue& list = root["items"];
	if (list.IsArray()) {
		for (size_t i = 0; i < list.Size(); ++i) {
			LootItemDef d = ReadItem(list[i], LootItemDef{});
			if (!d.id.empty()) items.push_back(d);
		}
	}
	items_ = std::move(items);
	ether_ = ReadItem(root["ether"], ether_);
	ether_.id = "Ether";

	std::vector<LootTable> tables;
	const JsonValue& drops = root["drops"];
	if (drops.IsArray()) {
		for (size_t i = 0; i < drops.Size(); ++i) {
			LootTable t = ReadTable(drops[i]);
			if (!t.prefab.empty()) tables.push_back(std::move(t));
		}
	}
	tables_ = std::move(tables);
	boss_ = ReadTable(root["boss"]);

	const JsonValue& m = root["motion"];
	if (m.IsObject()) {
		auto read = [&m](const char* key, float& v) { v = static_cast<float>(m[key].AsDouble(v)); };
		read("popSpeedMin", motion_.popSpeedMin);
		read("popSpeedMax", motion_.popSpeedMax);
		read("popDamping", motion_.popDamping);
		read("popTime", motion_.popTime);
		read("homeRate", motion_.homeRate);
		read("homeAccel", motion_.homeAccel);
		read("homeMinSpeed", motion_.homeMinSpeed);
		read("collectRadius", motion_.collectRadius);
		read("maxLifetime", motion_.maxLifetime);
		read("aimHeight", motion_.aimHeight);
		read("appearTime", motion_.appearTime);
		motion_.maxPieces = static_cast<int>(m["maxPieces"].AsInt(motion_.maxPieces));
	}
	return true;
}
