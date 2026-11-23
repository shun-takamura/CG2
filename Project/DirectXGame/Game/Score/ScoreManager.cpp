#include "ScoreManager.h"

#ifdef USE_IMGUI
#include "imgui.h"
#endif

namespace {
	// デバッグのボタンで足すスコア（小）
	constexpr int kDebugScoreAddSmall = 100;
	// デバッグのボタンで足すスコア（大）
	constexpr int kDebugScoreAddLarge = 1000;
}

void ScoreManager::OnImGui()
{
#ifdef USE_IMGUI
	ImGui::Text("Score: %d", score_);
	if (ImGui::Button("Reset")) Reset();
	ImGui::SameLine();
	if (ImGui::Button("+100")) AddScore(kDebugScoreAddSmall);
	ImGui::SameLine();
	if (ImGui::Button("+1000")) AddScore(kDebugScoreAddLarge);
#endif
}
