#include "ScoreManager.h"

#ifdef USE_IMGUI
#include "imgui.h"
#endif

ScoreManager* ScoreManager::GetInstance()
{
	static ScoreManager instance;
	return &instance;
}

void ScoreManager::OnImGui()
{
#ifdef USE_IMGUI
	ImGui::Text("Score: %d", score_);
	if (ImGui::Button("Reset")) Reset();
	ImGui::SameLine();
	if (ImGui::Button("+100")) AddScore(100);
	ImGui::SameLine();
	if (ImGui::Button("+1000")) AddScore(1000);
#endif
}
