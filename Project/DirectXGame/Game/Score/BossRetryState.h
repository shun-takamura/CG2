#pragma once

/// <summary>
/// ボス戦のリトライに使うシーン跨ぎの状態を保持する（GameSession が 1 つだけ持つ）。
/// StagePlayScene がボス戦開始時にスナップショット（HP・必殺技ゲージ）を保存し、
/// GameOverScene の「ボス戦からリトライ」選択で StagePlayScene 側がそれを読み出して復元する。
/// </summary>
class BossRetryState {
public:
	/// <summary>チェックポイント無しで作る</summary>
	BossRetryState() = default;
	~BossRetryState() = default;


	/// <summary>ボス戦（Phase::Boss）突入時に呼ぶ：以後のリトライで使うスナップショットを保存する</summary>
	/// <param name="playerHP">自機の HP</param>
	/// <param name="specialGauge">必殺技ゲージ</param>
	void SaveCheckpoint(int playerHP, float specialGauge) {
		hasCheckpoint_ = true;
		playerHP_ = playerHP;
		specialGauge_ = specialGauge;
	}
	/// <summary>スナップショットを「無い」に戻す</summary>
	void ClearCheckpoint() { hasCheckpoint_ = false; }
	/// <summary>スナップショットがあるか</summary>
	/// <returns>保存済みなら true</returns>
	bool HasCheckpoint() const { return hasCheckpoint_; }
	/// <summary>保存した自機の HP を取得する</summary>
	/// <returns>自機の HP</returns>
	int GetCheckpointPlayerHP() const { return playerHP_; }
	/// <summary>保存した必殺技ゲージを取得する</summary>
	/// <returns>必殺技ゲージ</returns>
	float GetCheckpointSpecialGauge() const { return specialGauge_; }

	/// <summary>ボス戦からのリトライを頼む（GameOverScene が選択結果を書き込み、StagePlayScene::Initialize が読む）</summary>
	void RequestBossRetry() { retryRequested_ = true; }
	/// <summary>リトライの要求を受け取る（受け取ったら要求は消える）</summary>
	/// <returns>要求があれば true</returns>
	bool ConsumeBossRetryRequest() {
		bool v = retryRequested_;
		retryRequested_ = false;
		return v;
	}

private:
	BossRetryState(const BossRetryState&) = delete;
	BossRetryState& operator=(const BossRetryState&) = delete;

	bool hasCheckpoint_ = false;
	int playerHP_ = 0;
	float specialGauge_ = 0.0f;
	bool retryRequested_ = false;
};
