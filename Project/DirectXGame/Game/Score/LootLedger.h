#pragma once

#include <deque>
#include <string>
#include <vector>

/// <summary>手に入れた物の出どころ（リザルトで「ステージ報酬」と「部位破壊報酬」を分けて見せる。23_StageClear.md §2）</summary>
enum class LootSource {
	Stage,      // 敵・ボスの撃破など（ステージ報酬）
	PartBreak,  // ボスの部位破壊（部位破壊報酬）
};

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
	/// <summary>アイテムを足す（itemId が "Ether" ならエーテル）。source が PartBreak の素材は部位破壊報酬にも数える</summary>
	void Add(const std::string& itemId, int count, LootSource source = LootSource::Stage);
	/// <summary>ボスの報酬（LootCatalog の boss）を足す。エーテルには難易度の報酬倍率を掛ける</summary>
	void AddBossReward();

	/// <summary>アイテムの合計（LootCatalog の id）。出どころを問わない</summary>
	int GetItemCount(const std::string& itemId) const;
	/// <summary>ステージ報酬の分（合計から部位破壊の分を引いた数）</summary>
	int GetStageItemCount(const std::string& itemId) const;
	/// <summary>部位破壊報酬の分</summary>
	int GetPartBreakItemCount(const std::string& itemId) const;
	int GetEther() const { return ether_; }

	/// <summary>最近拾った物を古い順に 1 つ取り出す。無ければ false</summary>
	bool PopPickup(LootPickup& out);

	void OnImGui();

	/// <summary>集計の 1 行（アイテム id と数）</summary>
	struct Entry {
		std::string itemId;
		int count = 0;
	};

private:
	LootLedger() = default;
	~LootLedger() = default;
	LootLedger(const LootLedger&) = delete;
	LootLedger& operator=(const LootLedger&) = delete;

	std::vector<Entry> items_;           // 合計（出どころを問わない）
	std::vector<Entry> partBreakItems_;  // そのうち部位破壊の分（エーテルは持たない＝合計にだけ入る）
	int ether_ = 0;
	std::deque<LootPickup> pickups_;
};
