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
	constexpr const char* kTuningPath = "Resources/Json/Tuning/EtherState.json";

	void ReadFloat(const JsonValue& node, const char* key, float& value) {
		value = static_cast<float>(node[key].AsDouble(value));
	}
	void WriteFloat(JsonValue& node, const char* key, float value) {
		node[key] = JsonValue(static_cast<double>(value));
	}
}

EtherState* EtherState::GetInstance() {
	static EtherState instance;
	return &instance;
}

EtherState::EtherState() {
	LoadTuning();
	Reset();
}

void EtherState::Reset() {
	hand_ = initialHand_;
	bet_ = 0.0f;
	betPlaced_ = false;
	difficulty_ = initialDifficulty_;
	PlaceDefaultBetIfEmpty();
}

void EtherState::PlaceDefaultBetIfEmpty() {
	// 賭ける画面を通らずに始めた（デバッグの起動など）ときの代わり
	if (betPlaced_) return;
	PlaceBet(initialDifficulty_);
}

float EtherState::CostOf(float difficulty) const {
	const float n = std::clamp(difficulty, difficultyMin_, difficultyMax_);
	return (std::max)(costScale_ * (n - 1.0f) * (n + 2.0f), 0.0f);
}

float EtherState::GetMaxAffordableDifficulty() const {
	if (infiniteHand_) return difficultyMax_;
	// 今の賭け分は手元へ戻してから引くので、使えるのは手元＋賭け分。消費は単調に増えるので 0.1 刻みで下から探す
	const float budget = hand_ + bet_;
	float best = difficultyMin_;
	for (float d = difficultyMin_; d <= difficultyMax_ + 1e-4f; d += 0.1f) {
		if (CostOf(d) > budget + 1e-3f) break;
		best = d;
	}
	return best;
}

void EtherState::PlaceBet(float difficulty) {
	difficulty_ = std::clamp(difficulty, difficultyMin_, difficultyMax_);
	const float cost = CostOf(difficulty_);
	if (!infiniteHand_) {
		hand_ += bet_;
		hand_ = (std::max)(hand_ - cost, 0.0f);
	}
	bet_ = cost;
	betPlaced_ = true;
}

EtherState::DeathResult EtherState::ApplyDeath() {
	PlaceDefaultBetIfEmpty();
	DeathResult result;
	result.betBefore = bet_;
	result.difficultyBefore = difficulty_;
	// 減った分は「戻ってくるために使った」（§2.1）。手元へは戻さない
	bet_ = (std::max)(bet_ * (1.0f - std::clamp(betLossRatio_, 0.0f, 1.0f)), 0.0f);
	// 元の純度 ÷ 1.17 を小数第 2 位以下で切り捨て（例 5.0 → 4.2 → 3.5 → 2.9）。÷ の誤差で 1 つ下へ落ちないよう少し足してから切る
	const float divided = difficulty_ / (std::max)(difficultyDivisor_, 1.0f);
	difficulty_ = (std::max)(std::floor(divided * 10.0f + 1e-3f) / 10.0f, difficultyMin_);
	betPlaced_ = bet_ > 1e-4f;
	result.betAfter = bet_;
	result.difficultyAfter = difficulty_;
	return result;
}

void EtherState::Withdraw() {
	if (!infiniteHand_) hand_ += bet_;
	bet_ = 0.0f;
	betPlaced_ = false;
}

float EtherState::BetToFill(float bet) const {
	const float capacity = (std::max)(CostOf(difficultyMax_), 1e-3f);
	return std::pow(std::clamp(bet / capacity, 0.0f, 1.0f), (std::max)(fillExponent_, 0.05f));
}

float EtherState::DifficultyTo01(float difficulty) const {
	const float range = (std::max)(difficultyMax_ - difficultyMin_, 1e-3f);
	return std::clamp((difficulty - difficultyMin_) / range, 0.0f, 1.0f);
}

bool EtherState::LoadTuning() {
	if (!std::filesystem::exists(kTuningPath)) return false;
	const auto result = JsonParser::ParseFile(kTuningPath);
	if (!result.success) return false;
	const JsonValue& root = result.value;
	ReadFloat(root, "initialHand", initialHand_);
	ReadFloat(root, "initialDifficulty", initialDifficulty_);
	infiniteHand_ = root["infiniteHand"].AsBool(infiniteHand_);
	ReadFloat(root, "costScale", costScale_);
	ReadFloat(root, "fillExponent", fillExponent_);
	ReadFloat(root, "betLossRatio", betLossRatio_);
	ReadFloat(root, "difficultyDivisor", difficultyDivisor_);
	ReadFloat(root, "difficultyMin", difficultyMin_);
	ReadFloat(root, "difficultyMax", difficultyMax_);
	return true;
}

void EtherState::SaveTuning() const {
	JsonValue root = JsonValue::MakeObject();
	root["_comment"] = JsonValue(std::string("19_GameOverAndDifficulty.md §4.8 G6。エーテルの状態の初期値と、死んだときの減り方。状態そのものはセーブしない"));
	WriteFloat(root, "initialHand", initialHand_);
	WriteFloat(root, "initialDifficulty", initialDifficulty_);
	root["infiniteHand"] = JsonValue(infiniteHand_);
	WriteFloat(root, "costScale", costScale_);
	WriteFloat(root, "fillExponent", fillExponent_);
	WriteFloat(root, "betLossRatio", betLossRatio_);
	WriteFloat(root, "difficultyDivisor", difficultyDivisor_);
	WriteFloat(root, "difficultyMin", difficultyMin_);
	WriteFloat(root, "difficultyMax", difficultyMax_);
	JsonWriter::WriteFile(kTuningPath, root, { true, 2 });
}

void EtherState::OnImGui() {
#ifdef _DEBUG
	// 本番は HUB の賭ける画面（§4.7）。ここはデバッグで直接決める
	ImGui::Text("Hand %.1f  Bet %.1f  Difficulty %.2f  Placed %d", hand_, bet_, difficulty_, betPlaced_ ? 1 : 0);
	static float betDifficulty = 5.0f;
	ImGui::SliderFloat("Purity##EtherStateBet", &betDifficulty, difficultyMin_, difficultyMax_, "%.1f");
	ImGui::SameLine();
	if (ImGui::Button("Place Bet##EtherState")) PlaceBet(betDifficulty);
	ImGui::DragFloat("Bet##EtherState", &bet_, 1.0f, 0.0f, CostOf(difficultyMax_));
	ImGui::SliderFloat("Difficulty##EtherState", &difficulty_, difficultyMin_, difficultyMax_);
	if (ImGui::Button("Reset##EtherState")) Reset();
	ImGui::SameLine();
	if (ImGui::Button("Apply Death##EtherState")) ApplyDeath();
	ImGui::SameLine();
	if (ImGui::Button("Withdraw##EtherState")) Withdraw();
	ImGui::SeparatorText("Tuning");
	ImGui::DragFloat("Initial Hand", &initialHand_, 1.0f, 0.0f, 10000.0f);
	ImGui::DragFloat("Initial Difficulty", &initialDifficulty_, 0.1f, difficultyMin_, difficultyMax_);
	ImGui::Checkbox("Infinite Hand", &infiniteHand_);
	ImGui::DragFloat("Cost Scale (x(n-1)(n+2))", &costScale_, 1.0f, 0.0f, 10000.0f);
	ImGui::DragFloat("Fill Exponent", &fillExponent_, 0.01f, 0.05f, 3.0f);
	ImGui::Text("Cost: I %.0f  V %.0f  IX %.0f", CostOf(difficultyMin_), CostOf(5.0f), CostOf(difficultyMax_));
	ImGui::SliderFloat("Bet Loss Ratio (death)", &betLossRatio_, 0.0f, 1.0f);
	ImGui::DragFloat("Difficulty Divisor (death)", &difficultyDivisor_, 0.01f, 1.0f, 3.0f);
	ImGui::DragFloat("Difficulty Min (I)", &difficultyMin_, 0.1f, 0.0f, 9.0f);
	ImGui::DragFloat("Difficulty Max (IX)", &difficultyMax_, 0.1f, 0.0f, 20.0f);
	if (ImGui::Button("Save##EtherStateTuning")) SaveTuning();
	ImGui::SameLine();
	if (ImGui::Button("Reload##EtherStateTuning")) LoadTuning();
	ImGui::SameLine();
	ImGui::TextDisabled("%s", kTuningPath);
#endif
}
