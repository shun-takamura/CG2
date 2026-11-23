#include "LootCatalog.h"

#include "Json/JsonValue.h"
#include "Json/JsonVector.h"
#include "Json/JsonParser.h"
#include <filesystem>

namespace {
	constexpr const char* kTuningPath = "Resources/Json/Tuning/Loot.json";
	// レア度の上限
	constexpr int kMaxRarity = 5;
	// エーテルの色（淡い水色）の緑の成分
	constexpr float kEtherColorGreen = 0.641f;

	/// <summary>アイテム 1 種の定義を読む（レア度は 1〜5 に収める）</summary>
	/// <param name="node">読む JSON オブジェクト</param>
	/// <param name="fallback">無いキーに使う値</param>
	/// <returns>読んだ定義。オブジェクトでなければ fallback</returns>
	LootItemDef ReadItem(const JsonValue& node, const LootItemDef& fallback) {
		LootItemDef d = fallback;
		if (!node.IsObject()) return d;
		d.id = node["id"].AsString(d.id);
		d.displayName = node["displayName"].AsString(d.id);
		d.prefab = node["prefab"].AsString(d.prefab);
		d.color = ReadVector4(node["color"], d.color);
		d.scale = static_cast<float>(node["scale"].AsDouble(d.scale));
		d.spinSpeed = static_cast<float>(node["spinSpeed"].AsDouble(d.spinSpeed));
		d.description = node["description"].AsString(d.description);
		d.rarity = static_cast<int>(node["rarity"].AsInt(d.rarity));
		if (d.rarity < 1) d.rarity = 1;
		if (d.rarity > kMaxRarity) d.rarity = kMaxRarity;
		d.sellPrice = static_cast<int>(node["sellPrice"].AsInt(d.sellPrice));
		return d;
	}

	/// <summary>敵 1 種のドロップの表を読む（id が空の行は捨てる）</summary>
	/// <param name="node">読む JSON オブジェクト</param>
	/// <returns>読んだ表。オブジェクトでなければ空の表</returns>
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
	ether_.color = { 0.0f, kEtherColorGreen, 1.0f, 1.0f };
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
