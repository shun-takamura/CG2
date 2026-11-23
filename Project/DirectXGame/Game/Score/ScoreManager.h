#pragma once
#include <cstdint>

/// <summary>
/// ステージ中の累積スコアを保持する（GameSession が 1 つだけ持つ）。
/// 現状はフラットに「敵 1 体撃破で +100」など固定スコアを想定。
/// 将来は敵種別ごとの基礎点・倍率を引数化する想定。
/// </summary>
class ScoreManager {
public:
	/// <summary>スコア 0・撃破数 0 で作る</summary>
	ScoreManager() = default;
	~ScoreManager() = default;


	/// <summary>スコアと撃破数を 0 に戻す</summary>
	void Reset() { score_ = 0; killCount_ = 0; }
	/// <summary>スコアを足す（0 未満にはしない）</summary>
	/// <param name="amount">足す点数（負なら引く）</param>
	void AddScore(int amount) { score_ += amount; if (score_ < 0) score_ = 0; }
	/// <summary>今のスコアを取得する</summary>
	/// <returns>スコア</returns>
	int  GetScore() const { return score_; }

	/// <summary>撃破数を足す</summary>
	/// <param name="count">足す数</param>
	void AddKill(int count = 1) { killCount_ += count; }
	/// <summary>今の撃破数を取得する</summary>
	/// <returns>撃破数</returns>
	int  GetKillCount() const { return killCount_; }

	/// <summary>スコアと撃破数を ImGui に出す（ImGui タブから呼ぶ）</summary>
	void OnImGui();

private:
	ScoreManager(const ScoreManager&) = delete;
	ScoreManager& operator=(const ScoreManager&) = delete;

	int score_ = 0;
	int killCount_ = 0;
};
