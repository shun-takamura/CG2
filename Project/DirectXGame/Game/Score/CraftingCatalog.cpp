#include "CraftingCatalog.h"

#include "Json/JsonValue.h"
#include "Json/JsonParser.h"
#include <filesystem>

namespace {
	constexpr const char* kTuningPath = "Resources/Json/Tuning/Crafting.json";

	CraftCost ReadCost(const JsonValue& node, bool initialFallback) {
		CraftCost c;
		c.initial = initialFallback;
		if (!node.IsObject()) return c;
		c.initial = node["initial"].AsBool(initialFallback);
		c.ether = static_cast<int>(node["ether"].AsInt(0));
		const JsonValue& list = node["materials"];
		if (list.IsArray()) {
			for (size_t i = 0; i < list.Size(); ++i) {
				const JsonValue& m = list[i];
				if (!m.IsObject()) continue;
				CraftCost::Material mat;
				mat.itemId = m["id"].AsString(std::string());
				mat.count = static_cast<int>(m["count"].AsInt(0));
				if (!mat.itemId.empty() && mat.count > 0) c.materials.push_back(mat);
			}
		}
		return c;
	}
}

CraftingCatalog* CraftingCatalog::GetInstance() {
	static CraftingCatalog instance;
	return &instance;
}

CraftingCatalog::CraftingCatalog() {
	Load();
}

const CraftCost* CraftingCatalog::FindIn(const std::vector<Entry>& list, const std::string& id, int level) {
	for (const Entry& e : list) {
		if (e.id != id) continue;
		const int index = level - 1;
		if (index < 0 || index >= static_cast<int>(e.levels.size())) return nullptr;
		return &e.levels[static_cast<size_t>(index)];
	}
	return nullptr;
}

const CraftCost* CraftingCatalog::FindMainWeapon(const std::string& id) const {
	return FindIn(mainWeapons_, id, 1);
}

const CraftCost* CraftingCatalog::FindShield(const std::string& id) const {
	return FindIn(shields_, id, 1);
}

const CraftCost* CraftingCatalog::FindFlightUnit(const std::string& unitId, int level) const {
	return FindIn(flightUnits_, unitId, level);
}

bool CraftingCatalog::Load() {
	if (!std::filesystem::exists(kTuningPath)) return false;
	const auto result = JsonParser::ParseFile(kTuningPath);
	if (!result.success) return false;
	const JsonValue& root = result.value;

	// 武器・盾：{ id, initial | materials+ether }。飛行ユニット：{ id, initial } か { id, levels:[...] }
	auto readList = [&root](const char* key, std::vector<Entry>& out) {
		out.clear();
		const JsonValue& list = root[key];
		if (!list.IsArray()) return;
		for (size_t i = 0; i < list.Size(); ++i) {
			const JsonValue& node = list[i];
			if (!node.IsObject()) continue;
			Entry e;
			e.id = node["id"].AsString(std::string());
			if (e.id.empty()) continue;
			const JsonValue& levels = node["levels"];
			if (levels.IsArray()) {
				for (size_t l = 0; l < levels.Size(); ++l) e.levels.push_back(ReadCost(levels[l], false));
			} else {
				e.levels.push_back(ReadCost(node, false));
			}
			out.push_back(std::move(e));
		}
	};
	readList("mainWeapons", mainWeapons_);
	readList("shields", shields_);
	readList("flightUnits", flightUnits_);
	return true;
}
