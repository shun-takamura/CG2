#pragma once

// ボス戦のリトライに使うシーン跨ぎの状態を保持するシングルトン。
// StagePlayScene がボス戦開始時にスナップショット（HP・必殺技ゲージ）を保存し、
// GameOverScene の「ボス戦からリトライ」選択で StagePlayScene 側がそれを読み出して復元する。
class BossRetryState {
public:
	static BossRetryState* GetInstance();

	// ボス戦（Phase::Boss）突入時に呼ぶ：以後のリトライで使うスナップショットを保存。
	void SaveCheckpoint(int playerHP, float specialGauge) {
		hasCheckpoint_ = true;
		playerHP_ = playerHP;
		specialGauge_ = specialGauge;
	}
	void ClearCheckpoint() { hasCheckpoint_ = false; }
	bool HasCheckpoint() const { return hasCheckpoint_; }
	int GetCheckpointPlayerHP() const { return playerHP_; }
	float GetCheckpointSpecialGauge() const { return specialGauge_; }

	// GameOverScene が選択結果を書き込み、StagePlayScene::Initialize が読む。
	void RequestBossRetry() { retryRequested_ = true; }
	bool ConsumeBossRetryRequest() {
		bool v = retryRequested_;
		retryRequested_ = false;
		return v;
	}

private:
	BossRetryState() = default;
	~BossRetryState() = default;
	BossRetryState(const BossRetryState&) = delete;
	BossRetryState& operator=(const BossRetryState&) = delete;

	bool hasCheckpoint_ = false;
	int playerHP_ = 0;
	float specialGauge_ = 0.0f;
	bool retryRequested_ = false;
};
