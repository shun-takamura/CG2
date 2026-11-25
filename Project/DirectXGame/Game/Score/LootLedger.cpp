#include "LootLedger.h"

#include "LootCatalog.h"
#include "DifficultyProfile.h"
#include "RandomGenerator.h"

#ifdef _DEBUG
#include "imgui.h"
#endif

namespace {
	// 表示が取り出さない場面（表示の無いシーン）で列が伸び続けないように
	constexpr size_t kMaxPendingPickups = 64;
	// デバッグのボタンで足すエーテルの量
	constexpr int kDebugEtherAdd = 50;
}

LootLedger::LootLedger(const DifficultyProfile* difficultyProfile) : difficultyProfile_(difficultyProfile) {}

void LootLedger::Reset() {
	items_.clear();
	partBreakItems_.clear();
	ether_ = 0;
	pickups_.clear();
}

namespace {
	/// <summary>一覧のアイテムに個数を足す（無ければ行を足す）</summary>
	/// <param name="list">足す一覧</param>
	/// <param name="itemId">アイテムの id</param>
	/// <param name="count">足す個数</param>
	void AddEntry(std::vector<LootLedger::Entry>& list, const std::string& itemId, int count) {
		for (LootLedger::Entry& e : list) {
			if (e.itemId == itemId) {
				e.count += count;
				return;
			}
		}
		list.push_back({ itemId, count });
	}

	/// <summary>一覧からアイテムの個数を引く</summary>
	/// <param name="list">引く一覧</param>
	/// <param name="itemId">アイテムの id</param>
	/// <returns>個数。無ければ 0</returns>
	int FindEntry(const std::vector<LootLedger::Entry>& list, const std::string& itemId) {
		for (const LootLedger::Entry& e : list) {
			if (e.itemId == itemId) return e.count;
		}
		return 0;
	}
}

void LootLedger::Add(const std::string& itemId, int count, LootSource source) {
	if (count <= 0 || itemId.empty()) return;
	if (itemId == kEtherId) {
		ether_ += count;
	} else {
		AddEntry(items_, itemId, count);
		if (source == LootSource::PartBreak) AddEntry(partBreakItems_, itemId, count);
	}
	pickups_.push_back({ itemId, count });
	while (pickups_.size() > kMaxPendingPickups) pickups_.pop_front();
}

void LootLedger::AddBossReward() {
	const LootTable& boss = LootCatalog::GetInstance()->GetBossTable();
	if (boss.ether > 0) {
		Add(kEtherId, DifficultyProfile::ScaleInt(boss.ether, difficultyProfile_->GetRewardScale()));
	}
	for (const LootRoll& roll : boss.rolls) {
		// ボスは確率 1 の想定。1 未満の行もふる（報酬はゲームの結果なので、ゲーム用の乱数で引く）
		if (roll.chance >= 1.0f || RandomGenerator::GetInstance()->NextFloat01() < roll.chance) Add(roll.itemId, roll.count);
	}
}

int LootLedger::GetItemCount(const std::string& itemId) const {
	if (itemId == kEtherId) return ether_;
	return FindEntry(items_, itemId);
}

int LootLedger::GetStageItemCount(const std::string& itemId) const {
	return GetItemCount(itemId) - GetPartBreakItemCount(itemId);
}

int LootLedger::GetPartBreakItemCount(const std::string& itemId) const {
	if (itemId == kEtherId) return 0;
	return FindEntry(partBreakItems_, itemId);
}

bool LootLedger::PopPickup(LootPickup& out) {
	if (pickups_.empty()) return false;
	out = pickups_.front();
	pickups_.pop_front();
	return true;
}

void LootLedger::OnImGui() {
#ifdef _DEBUG
	const LootCatalog* catalog = LootCatalog::GetInstance();
	for (int i = 0; i < catalog->GetItemCount(); ++i) {
		const LootItemDef* item = catalog->GetItem(i);
		ImGui::Text("%s : %d", item->displayName.c_str(), GetItemCount(item->id));
		ImGui::SameLine();
		ImGui::PushID(i);
		if (ImGui::SmallButton("+1")) Add(item->id, 1);
		ImGui::PopID();
	}
	ImGui::Text("%s : %d", catalog->GetEther().displayName.c_str(), ether_);
	ImGui::SameLine();
	if (ImGui::SmallButton("+50")) Add(kEtherId, kDebugEtherAdd);
	if (ImGui::Button("Reset Loot")) Reset();
	if (ImGui::Button("Reload Loot.json")) LootCatalog::GetInstance()->Load();
#endif
}
