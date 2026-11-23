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
/// シングルトンにする理由：JSON を一度だけ読む、読むだけのデータの表のため。
/// </summary>
class CraftingCatalog {
public:
	/// <summary>唯一のインスタンスを取得する</summary>
	/// <returns>インスタンス</returns>
	static CraftingCatalog* GetInstance();

	/// <summary>メイン武器の作成に要る物を引く</summary>
	/// <param name="id">メイン武器の id</param>
	/// <returns>要る物。表に無ければ nullptr</returns>
	const CraftCost* FindMainWeapon(const std::string& id) const;
	/// <summary>盾の作成に要る物を引く</summary>
	/// <param name="id">盾の id</param>
	/// <returns>要る物。表に無ければ nullptr</returns>
	const CraftCost* FindShield(const std::string& id) const;
	/// <summary>飛行ユニットのある段階の作成・強化に要る物を引く</summary>
	/// <param name="unitId">LoadoutState::GetFlightUnitName の名前</param>
	/// <param name="level">段階（1..。1＝作成、2 以上＝強化）</param>
	/// <returns>要る物。表に無い・段階が範囲外なら nullptr</returns>
	const CraftCost* FindFlightUnit(const std::string& unitId, int level) const;

	/// <summary>Resources/Json/Tuning/Crafting.json を読む</summary>
	/// <returns>読めたら true。ファイルが無い・パースに失敗したら false</returns>
	bool Load();

private:
	/// <summary>表を読み込む</summary>
	CraftingCatalog();
	~CraftingCatalog() = default;
	CraftingCatalog(const CraftingCatalog&) = delete;
	CraftingCatalog& operator=(const CraftingCatalog&) = delete;

	struct Entry {
		std::string id;
		std::vector<CraftCost> levels;   // 武器・盾は 1 つ
	};
	/// <summary>一覧から id と段階で引く</summary>
	/// <param name="list">引く一覧</param>
	/// <param name="id">装備の id</param>
	/// <param name="level">段階（1..）</param>
	/// <returns>要る物。無い・段階が範囲外なら nullptr</returns>
	static const CraftCost* FindIn(const std::vector<Entry>& list, const std::string& id, int level);

	std::vector<Entry> mainWeapons_;
	std::vector<Entry> shields_;
	std::vector<Entry> flightUnits_;
};
