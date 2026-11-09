#pragma once

#include <deque>
#include <string>
#include <vector>

/// <summary>拾った 1 回ぶん（右下の表示が受け取る）。itemId が "Ether" ならエーテル</summary>
struct LootPickup {
	std::string itemId;
	int count = 0;
};

/// <summary>
/// そのステージで手に入れた物の集計（22_ItemDrop.md）。ScoreManager と同じ置き方のシングルトン。
/// ステージの開始で Reset、リザルトが合計を読む。**展示用なので EtherState の所持には足さない**（ユーザー決定）。
/// 拾うたびに「最近拾った物」の列にも積み、右下の表示（LootPickupLog）が取り出す。
/// </summary>
class LootLedger {
public:
	static LootLedger* GetInstance();
	static constexpr const char* kEtherId = "Ether";

	void Reset();
	/// <summary>アイテムを足す（itemId が "Ether" ならエーテル）</summary>
	void Add(const std::string& itemId, int count);
	/// <summary>ボスの報酬（LootCatalog の boss）を足す。エーテルには難易度の報酬倍率を掛ける</summary>
	void AddBossReward();

	/// <summary>アイテムの合計（LootCatalog の id）</summary>
	int GetItemCount(const std::string& itemId) const;
	int GetEther() const { return ether_; }

	/// <summary>最近拾った物を古い順に 1 つ取り出す。無ければ false</summary>
	bool PopPickup(LootPickup& out);

	void OnImGui();

private:
	LootLedger() = default;
	~LootLedger() = default;
	LootLedger(const LootLedger&) = delete;
	LootLedger& operator=(const LootLedger&) = delete;

	struct Entry {
		std::string itemId;
		int count = 0;
	};
	std::vector<Entry> items_;
	int ether_ = 0;
	std::deque<LootPickup> pickups_;
};
