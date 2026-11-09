#include "LootLedger.h"

#include "LootCatalog.h"
#include "DifficultyProfile.h"
#include <cstdlib>

#ifdef _DEBUG
#include "imgui.h"
#endif

namespace {
	// 表示が取り出さない場面（表示の無いシーン）で列が伸び続けないように
	constexpr size_t kMaxPendingPickups = 64;
}

LootLedger* LootLedger::GetInstance() {
	static LootLedger instance;
	return &instance;
}

void LootLedger::Reset() {
	items_.clear();
	ether_ = 0;
	pickups_.clear();
}

void LootLedger::Add(const std::string& itemId, int count) {
	if (count <= 0 || itemId.empty()) return;
	if (itemId == kEtherId) {
		ether_ += count;
	} else {
		bool found = false;
		for (Entry& e : items_) {
			if (e.itemId == itemId) {
				e.count += count;
				found = true;
				break;
			}
		}
		if (!found) items_.push_back({ itemId, count });
	}
	pickups_.push_back({ itemId, count });
	while (pickups_.size() > kMaxPendingPickups) pickups_.pop_front();
}

void LootLedger::AddBossReward() {
	const LootTable& boss = LootCatalog::GetInstance()->GetBossTable();
	if (boss.ether > 0) {
		Add(kEtherId, DifficultyProfile::ScaleInt(boss.ether, DifficultyProfile::GetInstance()->GetRewardScale()));
	}
	for (const LootRoll& roll : boss.rolls) {
		// ボスは確率 1 の想定。1 未満の行も一応ふる（毎回同じでよいので乱数の種は持たない）
		if (roll.chance >= 1.0f || (static_cast<float>(std::rand()) / RAND_MAX) < roll.chance) Add(roll.itemId, roll.count);
	}
}

int LootLedger::GetItemCount(const std::string& itemId) const {
	if (itemId == kEtherId) return ether_;
	for (const Entry& e : items_) {
		if (e.itemId == itemId) return e.count;
	}
	return 0;
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
	if (ImGui::SmallButton("+50")) Add(kEtherId, 50);
	if (ImGui::Button("Reset Loot")) Reset();
	if (ImGui::Button("Reload Loot.json")) LootCatalog::GetInstance()->Load();
#endif
}
