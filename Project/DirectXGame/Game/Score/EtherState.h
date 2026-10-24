#pragma once

/// <summary>
/// エーテル（通貨）の状態：手元・賭け分・難易度の 3 つだけ（19_GameOverAndDifficulty.md §4.8、G6）。
/// シーンをまたいで持つシングルトン（BossRetryState と同じ置き方）。**セーブはしない**（ゲームを起動している間だけ。2026-10-24 ユーザー）。
/// 賭ける画面（§4.7）ができるまでは ImGui で賭け分と難易度を決める。
/// 死んだら賭け分が少し減り、難易度が少し下がる（ApplyDeath。割合・下がる幅は JSON）。画面には数字を出さない（§3）。
/// </summary>
class EtherState {
public:
	static EtherState* GetInstance();

	/// <summary>死ぬ前と後の値（ゲームオーバー画面が液面と針を動かすのに使う）</summary>
	struct DeathResult {
		float betBefore = 0.0f;
		float betAfter = 0.0f;
		float difficultyBefore = 0.0f;
		float difficultyAfter = 0.0f;
	};

	/// <summary>
	/// 死んだときに 1 回呼ぶ（ゲームオーバー画面の入口）。賭け分を減らし、難易度を下げる。
	/// 賭け分が空なら先に既定の賭け分を置く（賭ける画面の代わり。引き上げた後にまた遊んでも器が空にならないように）
	/// </summary>
	DeathResult ApplyDeath();
	/// <summary>「引き上げる」：賭け分を手元へ戻す</summary>
	void Withdraw();
	/// <summary>初期値（JSON）に戻す</summary>
	void Reset();

	float GetHand() const { return hand_; }
	float GetBet() const { return bet_; }
	float GetDifficulty() const { return difficulty_; }
	float GetDifficultyMin() const { return difficultyMin_; }
	float GetDifficultyMax() const { return difficultyMax_; }

	/// <summary>賭け分 → 器の液体の量（体積の割合 0..1）</summary>
	float BetToFill(float bet) const;
	/// <summary>難易度 → 0..1（文字盤の I が 0、IX が 1）</summary>
	float DifficultyTo01(float difficulty) const;

	bool LoadTuning();
	void SaveTuning() const;
	void OnImGui();

private:
	EtherState();
	~EtherState() = default;
	EtherState(const EtherState&) = delete;
	EtherState& operator=(const EtherState&) = delete;

	void PlaceDefaultBetIfEmpty();

	float hand_ = 0.0f;
	float bet_ = 0.0f;
	float difficulty_ = 5.0f;

	// 調整値（Resources/Json/Tuning/EtherState.json）
	float initialHand_ = 100.0f;
	float initialBet_ = 60.0f;
	float initialDifficulty_ = 5.0f;
	float capacity_ = 100.0f;          // 器いっぱいの賭け分
	float betLossRatio_ = 0.2f;        // 死んだときに減る割合（賭け分に対して）
	float difficultyDrop_ = 0.5f;      // 死んだときに下がる難易度
	float difficultyMin_ = 1.0f;       // 文字盤の I（2026-10-24 ユーザー：左端は I から）
	float difficultyMax_ = 9.0f;       // 文字盤の IX
};
