#pragma once

/// <summary>
/// エーテル（通貨）の状態：手元・賭け分・難易度の 3 つだけ（19_GameOverAndDifficulty.md §4.8、G6）。
/// シーンをまたいで持つシングルトン（BossRetryState と同じ置き方）。**セーブはしない**（ゲームを起動している間だけ。2026-10-24 ユーザー）。
/// 賭け分と難易度（純度）は HUB の賭ける画面（§4.7、HubEtherBet）で PlaceBet が決める。純度の消費は 125(n−1)(n+2)（係数は JSON）。
/// 所持はコンテストの間は無限の扱い（infiniteHand。表示は 99,999）。
/// 死んだら賭け分が少し減り、純度（難易度）が ÷1.17 の 0.1 単位切り捨てで下がる（ApplyDeath。割合・割る数は JSON）。画面には数字を出さない（§3）。
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
	/// <summary>賭ける画面の決定：純度を決め、その消費を賭け分にする（前の賭け分は手元へ戻してから引く）</summary>
	void PlaceBet(float difficulty);
	/// <summary>純度 → 消費するエーテル（costScale·(n−1)(n+2)。I＝1.0 で 0）</summary>
	float CostOf(float difficulty) const;
	/// <summary>手元から賭けられる上限の純度（所持が無限なら IX）</summary>
	float GetMaxAffordableDifficulty() const;
	bool IsInfiniteHand() const { return infiniteHand_; }

	float GetHand() const { return hand_; }
	float GetBet() const { return bet_; }
	float GetDifficulty() const { return difficulty_; }
	float GetDifficultyMin() const { return difficultyMin_; }
	float GetDifficultyMax() const { return difficultyMax_; }

	/// <summary>賭け分 → 器の液体の量（体積の割合 0..1）。(賭け分 ÷ IX の消費)^fillExponent</summary>
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
	bool betPlaced_ = false;   // 賭けた（純度 I＝消費 0 でも賭けたことにする。空なら GameOver の入口で既定を置く）

	// 調整値（Resources/Json/Tuning/EtherState.json）
	float initialHand_ = 99999.0f;
	float initialDifficulty_ = 5.0f;
	bool infiniteHand_ = true;         // 所持を減らさない（コンテストの間。2026-10-27 ユーザー「今回は無限」）
	float costScale_ = 125.0f;         // 純度の消費 costScale·(n−1)(n+2)（2026-10-27 ユーザー：1→0・2→500・3→1250）
	float fillExponent_ = 0.5f;        // 液面＝(賭け分 ÷ IX の消費)^これ。0.5 で純度 5 が約 0.56（G6 の見た目に近い）
	float betLossRatio_ = 0.2f;        // 死んだときに減る割合（賭け分に対して）
	float difficultyDivisor_ = 1.17f;  // 死んだときの純度 = 切り捨て(純度 ÷ これ, 0.1 単位)（2026-10-27 ユーザー）
	float difficultyMin_ = 1.0f;       // 文字盤の I（2026-10-24 ユーザー：左端は I から）
	float difficultyMax_ = 9.0f;       // 文字盤の IX
};
