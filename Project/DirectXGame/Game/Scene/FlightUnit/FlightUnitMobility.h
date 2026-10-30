#pragma once
#include "FlightUnitPart.h"

/// <summary>
/// 機動系（20_Loadout.md §3.3。ジャスト回避を軸にする）。
/// 常時：移動が速くなる・ジャスト回避のときに必殺技ゲージを足す。
/// RB：数秒だけ World をスローにする（自分は Player グループなので普通の速さ）。カウンターは無い。
/// ジャスト回避・必殺技が起きたら、スローはそこで終わる（時間の倍率はそちらに任せる）。
/// </summary>
class FlightUnitMobility : public FlightUnitSkill {
public:
	bool Activate() override;
	void Cancel(bool restoreTime) override;
	bool IsActive() const override { return active_; }
	bool UsesTimeScale() const override { return true; }
	float GetActiveRatio() const override;
	float GetCooldown() const override { return cooldown_[LevelIndex()]; }
	void Update(float dt, bool canAct) override;

	float GetMoveSpeedScale() const override { return moveSpeedScale_[LevelIndex()]; }
	float GetJustDodgeGaugeBonus() const override { return justDodgeGaugeBonus_[LevelIndex()]; }
	float GetJustDodgeWindowBonus() const override { return justDodgeWindowBonus_[LevelIndex()]; }

	Vector4 GetColor() const override { return color_; }
	void LoadTuning(const JsonValue& node) override;
	void SaveTuning(JsonValue& node) const override;
	void OnImGui() override;

private:
	// 段階 1..3 の値
	float moveSpeedScale_[3]       = { 1.05f, 1.10f, 1.15f };
	float justDodgeGaugeBonus_[3]  = { 4.0f, 6.0f, 8.0f };
	float justDodgeWindowBonus_[3] = { 0.01f, 0.025f, 0.05f }; // ジャスト回避の受付を延ばす秒
	float slowDuration_[3]        = { 3.0f, 3.5f, 4.0f };
	float cooldown_[3]            = { 25.0f, 22.0f, 20.0f };
	float slowWorld_ = 0.35f;     // スロー中の World の倍率
	float easeIn_ = 0.12f;        // 入りと抜けの時間（いきなり変えない）
	float easeOut_ = 0.25f;
	Vector4 color_{ 0.45f, 0.95f, 0.85f, 1.0f };

	bool active_ = false;
	float timer_ = 0.0f;          // 経過
};
