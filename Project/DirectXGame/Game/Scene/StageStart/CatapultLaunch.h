#pragma once

class JsonValue;

/// <summary>
/// 発進の進み具合（16_StageStart.md §3.2、F5）。2026-10-19 ユーザー決定：
///   カタパルトの電気とスラスター（SPACE）は別のもの。
///   1. 射出口までネオンが緑に点き切ったら、カタパルトを充電する（自動。済んだら表示板に CHARGED と出すだけ）
///   2. 充電が済むと SPACE を受け付ける。SPACE はスラスターの出力で、長押しの間タコメーターが上がり、離すと下がる
///   3. 出力が 100% になったら射出。機体は溝に沿って加速し（前進量＝時間の累乗の曲線）、射出口で終わる
///   4. 射出の途中（前進量が射出口までの cablePurgeRatio）で給電ケーブルをパージする
/// 描画は持たない（表示板・タコメーター・機体の前進は StageStartPart が読む）。調整値は StagePlay.json の "stageStart" → "launch"。
/// </summary>
class CatapultLaunch {
public:
	enum class Phase {
		Idle,        // 機体確認の途中（まだ充電しない）
		Charging,    // カタパルトを充電中
		Charged,     // 充電済み：SPACE でスラスターの出力を上げる
		Launching,   // 射出中（前へ加速）
	};

	void Reset();
	/// <summary>ネオンが点き切った：充電を始める</summary>
	void BeginCharge();
	/// <summary>射出を始める（出力が 100% になったとき。デバッグの「Next State」からも）</summary>
	void BeginLaunch();
	/// <summary>thrustHeld＝SPACE を押している（Charged の間だけ効く）</summary>
	void Update(float deltaTime, bool thrustHeld);

	Phase GetPhase() const { return phase_; }
	float GetPhaseTime() const { return phaseTime_; }
	bool IsCharged() const { return phase_ == Phase::Charged || phase_ == Phase::Launching; }
	/// <summary>スラスターの出力 0..1（射出中は 1）</summary>
	float GetThrust() const { return thrust_; }
	/// <summary>出力が 100% になった（このフレームに射出を始めてよい）</summary>
	bool IsThrustFull() const { return phase_ == Phase::Charged && thrust_ >= 1.0f; }
	/// <summary>一度でも出力を上げ始めた（機体を前傾にする）</summary>
	bool HasThrustStarted() const { return thrustStarted_; }
	/// <summary>タコメーターの回転数（表示用。出力に少しの揺れを足す）</summary>
	float GetRpm() const;
	float GetMaxRpm() const { return maxRpm_; }
	float GetRedZone() const { return redZone_; }

	/// <summary>射出口までの前進量 [m]（StageStartGeometry の tunnelEndZ。射出口の床の中心に機体の原点が来る所）</summary>
	void SetExitTravel(float travel) { exitTravel_ = travel; }
	/// <summary>機体の前進量 [m]（射出を始めてから。射出口を過ぎても最後の速さで進む）</summary>
	float GetTravel() const;
	/// <summary>前進の速さ [m/s]</summary>
	float GetSpeed() const;
	/// <summary>射出口での速さ [m/s]（いちばん速い。画角・ラジアルブラーの基準）</summary>
	float GetMaxSpeed() const;
	bool ShouldPurgeCable() const { return phase_ == Phase::Launching && GetTravel() >= exitTravel_ * cablePurgeRatio_; }
	bool HasReachedExit() const { return phase_ == Phase::Launching && phaseTime_ >= launchSeconds_; }
	/// <summary>射出口を過ぎてからの秒（過ぎていなければ負）</summary>
	float GetTimeSinceExit() const { return phase_ == Phase::Launching ? phaseTime_ - launchSeconds_ : -1.0f; }

	float GetLaunchShakeIntensity() const { return launchShakeIntensity_; }
	float GetLaunchShakeSeconds() const { return launchShakeSeconds_; }

	void LoadTuning(const JsonValue& node);
	void SaveTuning(JsonValue& node) const;
	void OnImGui();

private:
	Phase phase_ = Phase::Idle;
	float phaseTime_ = 0.0f;
	float thrust_ = 0.0f;
	bool thrustStarted_ = false;
	float time_ = 0.0f;
	float exitTravel_ = 160.0f;

	// 調整値
	float chargeSeconds_ = 2.0f;          // カタパルトの充電
	float thrustRiseSeconds_ = 1.8f;      // 押し続けて 0 → 100% まで
	float thrustFallSeconds_ = 1.0f;      // 離して 100% → 0 まで
	float thrustRiseCurve_ = 1.3f;        // 上がり方（1 で一定。大きいほど上の方で伸びが鈍る＝回転数らしい）
	float idleRpm_ = 900.0f;              // 出力 0 のときの回転数
	float maxRpm_ = 12000.0f;             // 出力 100%
	float redZone_ = 0.85f;               // タコメーターの赤の始まり（出力）
	float rpmJitter_ = 120.0f;            // 回転数の揺れ [rpm]
	float launchSeconds_ = 2.6f;          // 射出口までの秒
	float launchExponent_ = 2.2f;         // 前進量＝射出口 × (t / launchSeconds)^これ（大きいほど溜めてから一気に）
	float cablePurgeRatio_ = 0.7f;        // 射出口までのこの割合でケーブルをパージ（2026-10-19 ユーザー：7 割）
	float launchShakeIntensity_ = 0.12f;  // 射出の瞬間の揺れ
	float launchShakeSeconds_ = 0.5f;
};
