#pragma once
#include <cstdint>

// ステージ中の累積スコアを保持するシングルトン。
// 現状はフラットに「敵 1 体撃破で +100」など固定スコアを想定。
// 将来は敵種別ごとの基礎点・倍率を引数化する想定。
class ScoreManager {
public:
	static ScoreManager* GetInstance();

	void Reset() { score_ = 0; }
	void AddScore(int amount) { score_ += amount; if (score_ < 0) score_ = 0; }
	int  GetScore() const { return score_; }

	// ImGui タブから呼ぶ
	void OnImGui();

private:
	ScoreManager() = default;
	~ScoreManager() = default;
	ScoreManager(const ScoreManager&) = delete;
	ScoreManager& operator=(const ScoreManager&) = delete;

	int score_ = 0;
};
