#pragma once

#include <string>
#include <vector>

/// <summary>作成・強化 1 回に要る物（素材の個数とエーテル）</summary>
struct CraftCost {
	struct Material {
		std::string itemId;   // LootCatalog の id
		int count = 0;
	};
	bool initial = false;     // 初期装備（作らなくても持っている）
	std::vector<Material> materials;
	int ether = 0;
};

/// <summary>
/// 装備を作る・強めるのに要る物の表（22_ItemDrop.md）。Resources/Json/Tuning/Crafting.json を読む。
/// **展示用**：表示するだけで、素材やエーテルを減らしたり、作っていない装備を使えなくしたりはしない（ユーザー決定）。
/// メイン武器・盾は「作成」1 回、飛行ユニットは段階ごと（Lv1＝作成、Lv2・Lv3＝強化）。
/// </summary>
class CraftingCatalog {
public:
	static CraftingCatalog* GetInstance();

	/// <summary>表に無ければ nullptr</summary>
	const CraftCost* FindMainWeapon(const std::string& id) const;
	const CraftCost* FindShield(const std::string& id) const;
	/// <summary>unitId は LoadoutState::GetFlightUnitName の名前、level は 1..</summary>
	const CraftCost* FindFlightUnit(const std::string& unitId, int level) const;

	bool Load();

private:
	CraftingCatalog();
	~CraftingCatalog() = default;
	CraftingCatalog(const CraftingCatalog&) = delete;
	CraftingCatalog& operator=(const CraftingCatalog&) = delete;

	struct Entry {
		std::string id;
		std::vector<CraftCost> levels;   // 武器・盾は 1 つ
	};
	static const CraftCost* FindIn(const std::vector<Entry>& list, const std::string& id, int level);

	std::vector<Entry> mainWeapons_;
	std::vector<Entry> shields_;
	std::vector<Entry> flightUnits_;
};
