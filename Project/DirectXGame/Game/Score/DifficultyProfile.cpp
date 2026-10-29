#include "DifficultyProfile.h"

#include "EtherState.h"
#include "Json/JsonValue.h"
#include "Json/JsonParser.h"
#include "Json/JsonWriter.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>

#ifdef _DEBUG
#include "imgui.h"
#endif

namespace {
	constexpr const char* kTuningPath = "Resources/Json/Tuning/DifficultyProfile.json";

	void ReadCurve(const JsonValue& root, const char* key, DifficultyProfile::Curve& c) {
		const JsonValue& node = root[key];
		if (!node.IsObject()) return;
		c.atMin = static_cast<float>(node["I"].AsDouble(c.atMin));
		c.atMid = static_cast<float>(node["V"].AsDouble(c.atMid));
		c.atMax = static_cast<float>(node["IX"].AsDouble(c.atMax));
	}
	void WriteCurve(JsonValue& root, const char* key, const DifficultyProfile::Curve& c) {
		JsonValue node = JsonValue::MakeObject();
		node["I"] = JsonValue(static_cast<double>(c.atMin));
		node["V"] = JsonValue(static_cast<double>(c.atMid));
		node["IX"] = JsonValue(static_cast<double>(c.atMax));
		root[key] = node;
	}
}

DifficultyProfile* DifficultyProfile::GetInstance() {
	static DifficultyProfile instance;
	return &instance;
}

DifficultyProfile::DifficultyProfile() {
	LoadTuning();
}

int DifficultyProfile::ScaleInt(int base, float scale) {
	if (base <= 0) return base;
	return (std::max)(static_cast<int>(std::lround(static_cast<float>(base) * scale)), 1);
}

float DifficultyProfile::Evaluate(const Curve& c) const {
	return EvaluateAt(c, EtherState::GetInstance()->GetDifficulty());
}

float DifficultyProfile::EvaluateAt(const Curve& c, float purity) const {
	const EtherState* ether = EtherState::GetInstance();
	const float lo = ether->GetDifficultyMin();
	const float hi = ether->GetDifficultyMax();
	const float mid = std::clamp(midPurity_, lo, hi);
	const float p = std::clamp(purity, lo, hi);
	if (p <= mid) {
		const float t = (mid - lo) > 1e-4f ? (p - lo) / (mid - lo) : 1.0f;
		return c.atMin + (c.atMid - c.atMin) * t;
	}
	const float t = (hi - mid) > 1e-4f ? (p - mid) / (hi - mid) : 0.0f;
	return c.atMid + (c.atMax - c.atMid) * t;
}

bool DifficultyProfile::LoadTuning() {
	if (!std::filesystem::exists(kTuningPath)) return false;
	const auto result = JsonParser::ParseFile(kTuningPath);
	if (!result.success) return false;
	const JsonValue& root = result.value;
	midPurity_ = static_cast<float>(root["midPurity"].AsDouble(midPurity_));
	ReadCurve(root, "enemyHp", enemyHp_);
	ReadCurve(root, "bossHp", bossHp_);
	ReadCurve(root, "damage", damage_);
	ReadCurve(root, "shootInterval", shootInterval_);
	ReadCurve(root, "bossCooldown", bossCooldown_);
	return true;
}

void DifficultyProfile::SaveTuning() const {
	JsonValue root = JsonValue::MakeObject();
	root["_comment"] = JsonValue(std::string("19_GameOverAndDifficulty.md §5。純度 I・V・IX のときの倍率（間は直線）。純度は EtherState。shootInterval・bossCooldown は小さいほど速い"));
	root["midPurity"] = JsonValue(static_cast<double>(midPurity_));
	WriteCurve(root, "enemyHp", enemyHp_);
	WriteCurve(root, "bossHp", bossHp_);
	WriteCurve(root, "damage", damage_);
	WriteCurve(root, "shootInterval", shootInterval_);
	WriteCurve(root, "bossCooldown", bossCooldown_);
	JsonWriter::WriteFile(kTuningPath, root, { true, 2 });
}

void DifficultyProfile::OnImGui() {
#ifdef _DEBUG
	// 純度そのものは Ether State の「Place Bet」で変える。体力・射撃の間隔はその後にスポーンした敵から効く
	const float purity = EtherState::GetInstance()->GetDifficulty();
	ImGui::Text("Purity %.1f  ->  EnemyHP x%.2f  BossHP x%.2f  Damage x%.2f", purity,
		GetEnemyHpScale(), GetBossHpScale(), GetDamageScale());
	ImGui::Text("ShootInterval x%.2f  BossCooldown x%.2f  Reward x%.2f (G7)",
		GetShootIntervalScale(), GetBossCooldownScale(), GetRewardScale());
	ImGui::SeparatorText("I / V / IX");
	ImGui::DragFloat("V Purity##Difficulty", &midPurity_, 0.1f, 1.0f, 9.0f, "%.1f");
	auto curve = [](const char* label, Curve& c) {
		float v[3] = { c.atMin, c.atMid, c.atMax };
		if (ImGui::DragFloat3(label, v, 0.01f, 0.05f, 10.0f, "%.2f")) {
			c.atMin = v[0]; c.atMid = v[1]; c.atMax = v[2];
		}
	};
	curve("Enemy HP##Difficulty", enemyHp_);
	curve("Boss HP##Difficulty", bossHp_);
	curve("Damage##Difficulty", damage_);
	curve("Shoot Interval##Difficulty", shootInterval_);
	curve("Boss Cooldown##Difficulty", bossCooldown_);
	if (ImGui::Button("Save##DifficultyProfile")) SaveTuning();
	ImGui::SameLine();
	if (ImGui::Button("Reload##DifficultyProfile")) LoadTuning();
	ImGui::SameLine();
	ImGui::TextDisabled("%s", kTuningPath);
#endif
}
