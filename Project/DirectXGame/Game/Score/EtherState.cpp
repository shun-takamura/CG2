#include "EtherState.h"

#include "Json/JsonValue.h"
#include "Json/JsonParser.h"
#include "Json/JsonWriter.h"
#include <algorithm>
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
	difficulty_ = initialDifficulty_;
	PlaceDefaultBetIfEmpty();
}

void EtherState::PlaceDefaultBetIfEmpty() {
	if (bet_ > 1e-4f) return;
	const float bet = (std::min)(initialBet_, hand_);
	hand_ -= bet;
	bet_ = bet;
	difficulty_ = initialDifficulty_;
}

EtherState::DeathResult EtherState::ApplyDeath() {
	PlaceDefaultBetIfEmpty();
	DeathResult result;
	result.betBefore = bet_;
	result.difficultyBefore = difficulty_;
	// 減った分は「戻ってくるために使った」（§2.1）。手元へは戻さない
	bet_ = (std::max)(bet_ * (1.0f - std::clamp(betLossRatio_, 0.0f, 1.0f)), 0.0f);
	difficulty_ = (std::max)(difficulty_ - difficultyDrop_, difficultyMin_);
	result.betAfter = bet_;
	result.difficultyAfter = difficulty_;
	return result;
}

void EtherState::Withdraw() {
	hand_ += bet_;
	bet_ = 0.0f;
}

float EtherState::BetToFill(float bet) const {
	return std::clamp(bet / (std::max)(capacity_, 1e-3f), 0.0f, 1.0f);
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
	ReadFloat(root, "initialBet", initialBet_);
	ReadFloat(root, "initialDifficulty", initialDifficulty_);
	ReadFloat(root, "capacity", capacity_);
	ReadFloat(root, "betLossRatio", betLossRatio_);
	ReadFloat(root, "difficultyDrop", difficultyDrop_);
	ReadFloat(root, "difficultyMin", difficultyMin_);
	ReadFloat(root, "difficultyMax", difficultyMax_);
	return true;
}

void EtherState::SaveTuning() const {
	JsonValue root = JsonValue::MakeObject();
	root["_comment"] = JsonValue(std::string("19_GameOverAndDifficulty.md §4.8 G6。エーテルの状態の初期値と、死んだときの減り方。状態そのものはセーブしない"));
	WriteFloat(root, "initialHand", initialHand_);
	WriteFloat(root, "initialBet", initialBet_);
	WriteFloat(root, "initialDifficulty", initialDifficulty_);
	WriteFloat(root, "capacity", capacity_);
	WriteFloat(root, "betLossRatio", betLossRatio_);
	WriteFloat(root, "difficultyDrop", difficultyDrop_);
	WriteFloat(root, "difficultyMin", difficultyMin_);
	WriteFloat(root, "difficultyMax", difficultyMax_);
	JsonWriter::WriteFile(kTuningPath, root, { true, 2 });
}

void EtherState::OnImGui() {
#ifdef _DEBUG
	// 賭ける画面（§4.7）の代わり。ここで決めてからステージを始める
	ImGui::Text("Hand %.1f  Bet %.1f  Difficulty %.2f", hand_, bet_, difficulty_);
	ImGui::DragFloat("Bet##EtherState", &bet_, 0.5f, 0.0f, capacity_);
	ImGui::SliderFloat("Difficulty##EtherState", &difficulty_, difficultyMin_, difficultyMax_);
	if (ImGui::Button("Reset##EtherState")) Reset();
	ImGui::SameLine();
	if (ImGui::Button("Apply Death##EtherState")) ApplyDeath();
	ImGui::SameLine();
	if (ImGui::Button("Withdraw##EtherState")) Withdraw();
	ImGui::SeparatorText("Tuning");
	ImGui::DragFloat("Initial Hand", &initialHand_, 1.0f, 0.0f, 10000.0f);
	ImGui::DragFloat("Initial Bet", &initialBet_, 1.0f, 0.0f, 10000.0f);
	ImGui::DragFloat("Initial Difficulty", &initialDifficulty_, 0.1f, difficultyMin_, difficultyMax_);
	ImGui::DragFloat("Capacity (full vessel)", &capacity_, 1.0f, 1.0f, 10000.0f);
	ImGui::SliderFloat("Bet Loss Ratio (death)", &betLossRatio_, 0.0f, 1.0f);
	ImGui::DragFloat("Difficulty Drop (death)", &difficultyDrop_, 0.05f, 0.0f, 9.0f);
	ImGui::DragFloat("Difficulty Min (I)", &difficultyMin_, 0.1f, 0.0f, 9.0f);
	ImGui::DragFloat("Difficulty Max (IX)", &difficultyMax_, 0.1f, 0.0f, 20.0f);
	if (ImGui::Button("Save##EtherStateTuning")) SaveTuning();
	ImGui::SameLine();
	if (ImGui::Button("Reload##EtherStateTuning")) LoadTuning();
	ImGui::SameLine();
	ImGui::TextDisabled("%s", kTuningPath);
#endif
}
