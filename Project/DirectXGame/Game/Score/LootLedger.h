#pragma once

#include <deque>
#include <string>
#include <vector>

class DifficultyProfile;

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
/// そのステージで手に入れた物の集計（22_ItemDrop.md）。ScoreManager と同じく GameSession が 1 つだけ持つ。
/// ステージの開始で Reset、リザルトが合計を読む。**展示用なので EtherState の所持には足さない**（ユーザー決定）。
/// 拾うたびに「最近拾った物」の列にも積み、右下の表示（LootPickupLog）が取り出す。
/// </summary>
class LootLedger {
public:
	/// <summary>空の集計で作る</summary>
	/// <param name="difficultyProfile">ボスの報酬に掛ける純度の倍率を読む先（GameSession が持つ物）</param>
	explicit LootLedger(const DifficultyProfile* difficultyProfile);
	~LootLedger() = default;

	static constexpr const char* kEtherId = "Ether";

	/// <summary>集計と最近拾った物の列を空にする（ステージの開始）</summary>
	void Reset();
	/// <summary>アイテムを足す（itemId が "Ether" ならエーテル）。source が PartBreak の素材は部位破壊報酬にも数える</summary>
	/// <param name="itemId">アイテムの id（"Ether" ならエーテル。空なら何もしない）</param>
	/// <param name="count">個数（0 以下なら何もしない）</param>
	/// <param name="source">出どころ</param>
	void Add(const std::string& itemId, int count, LootSource source = LootSource::Stage);
	/// <summary>ボスの報酬（LootCatalog の boss）を足す。エーテルには難易度の報酬倍率を掛ける</summary>
	void AddBossReward();

	/// <summary>アイテムの合計を取得する。出どころを問わない</summary>
	/// <param name="itemId">LootCatalog の id（"Ether" ならエーテル）</param>
	/// <returns>合計の個数</returns>
	int GetItemCount(const std::string& itemId) const;
	/// <summary>ステージ報酬の分を取得する</summary>
	/// <param name="itemId">LootCatalog の id</param>
	/// <returns>合計から部位破壊の分を引いた数</returns>
	int GetStageItemCount(const std::string& itemId) const;
	/// <summary>部位破壊報酬の分を取得する</summary>
	/// <param name="itemId">LootCatalog の id</param>
	/// <returns>部位破壊の分の個数（エーテルは 0）</returns>
	int GetPartBreakItemCount(const std::string& itemId) const;
	/// <summary>手に入れたエーテルの合計を取得する</summary>
	/// <returns>エーテルの量</returns>
	int GetEther() const { return ether_; }

	/// <summary>最近拾った物を古い順に 1 つ取り出す</summary>
	/// <param name="out">取り出した物の書き込み先</param>
	/// <returns>取り出せたら true。無ければ false（out は書かない）</returns>
	bool PopPickup(LootPickup& out);

	/// <summary>集計を ImGui に出す</summary>
	void OnImGui();

	/// <summary>集計の 1 行（アイテム id と数）</summary>
	struct Entry {
		std::string itemId;
		int count = 0;
	};

private:
	LootLedger(const LootLedger&) = delete;
	LootLedger& operator=(const LootLedger&) = delete;

	// ボスの報酬に掛ける純度の倍率を読む先（GameSession が持つ物。LootLedger より長く生きる）
	const DifficultyProfile* difficultyProfile_ = nullptr;

	std::vector<Entry> items_;           // 合計（出どころを問わない）
	std::vector<Entry> partBreakItems_;  // そのうち部位破壊の分（エーテルは持たない＝合計にだけ入る）
	int ether_ = 0;
	std::deque<LootPickup> pickups_;
};
