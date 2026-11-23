#pragma once
#include "Enemy/Boss/IBossState.h"
#include "MathUtility.h"
#include "Scene/GameScene.h"

/// <summary>
/// 全方位リング：予兆→360°に N 発を一斉発射→半ピッチずらしてもう1周→硬直。
/// リングの基準角はプレイヤー方向に合わせ、1周目の弾が必ずプレイヤーへ向かうようにする。
/// </summary>
class BossAttackRadialBurst : public BossActionState {
	enum class Phase { Telegraph, Firing, Recover, Done };

public:
	/// <summary>全方位リングを作る</summary>
	/// <param name="bulletCount">1 周の弾数</param>
	/// <param name="telegraphTime">予兆の長さ [秒]</param>
	BossAttackRadialBurst(int bulletCount = 20, float telegraphTime = 1.0f)
		: bulletCount_(bulletCount), telegraphTime_(telegraphTime) {}

	/// <summary>予兆から始める（タイマーと撃ったリング数を 0 に）</summary>
	/// <param name="ctx">ボス・自機・シーンと共有データ</param>
	void OnEnter(BossStateContext& ctx) override {
		phase_ = Phase::Telegraph;
		timer_ = 0.0f;
		ringsFired_ = 0;
		ctx.billboardToPlayer = true;
	}

	/// <summary>予兆（黄の点滅）→リングを間隔を空けて撃つ→硬直を 1 フレーム進める</summary>
	/// <param name="dt">World の経過時間 [秒]</param>
	/// <param name="ctx">ボス・自機・シーンと共有データ</param>
	void Tick(float dt, BossStateContext& ctx) override {
		ctx.billboardToPlayer = true;
		timer_ += dt;

		if (phase_ == Phase::Telegraph) {
			SetBossTelegraphTint(ctx, { 1.0f, kTelegraphGreen, kTelegraphBlue }, timer_, telegraphTime_); // 弾幕系=黄 / 近接系=白
			if (timer_ >= telegraphTime_) {
				phase_ = Phase::Firing;
				timer_ = ringInterval_;
			}
		} else if (phase_ == Phase::Firing) {
			if (timer_ >= ringInterval_) {
				timer_ = 0.0f;
				FireRing(ctx);
				if (++ringsFired_ >= ringCount_) phase_ = Phase::Recover;
			}
		} else if (phase_ == Phase::Recover) {
			if (timer_ >= recoverTime_) phase_ = Phase::Done;
		}
	}

	/// <summary>全方位リングが終わったか</summary>
	/// <returns>硬直まで終わったら true</returns>
	bool IsFinished() const override { return phase_ == Phase::Done; }

private:
	// 予兆の点滅色（弾幕系＝黄）の緑の成分
	static constexpr float kTelegraphGreen = 0.9f;
	// 予兆の点滅色（弾幕系＝黄）の青の成分
	static constexpr float kTelegraphBlue = 0.2f;

	/// <summary>自機の方向を基準に 360° へ弾を 1 周撃つ。奇数周目は半ピッチずらす</summary>
	/// <param name="ctx">ボス・自機・シーンと共有データ</param>
	void FireRing(BossStateContext& ctx) {
		if (!ctx.scene || bulletCount_ <= 0) return;
		const BossAimInfo aim = ComputeBossAim(ctx);
		if (!aim.valid) return;
		const float step = kTwoPi / static_cast<float>(bulletCount_);
		const float base = aim.yaw + ((ringsFired_ % 2 == 1) ? step * 0.5f : 0.0f);
		for (int i = 0; i < bulletCount_; ++i) {
			const Vector3 dir = BossDirFromYawPitch(base + step * static_cast<float>(i), aim.pitch);
			ctx.scene->SpawnEnemyBullet(aim.origin, dir, -1.0f, -1.0f, "EnemyBullet", nullptr, 0.0f);
		}
	}

	Phase phase_ = Phase::Telegraph;
	float timer_ = 0.0f;
	int   ringsFired_ = 0;

	int   bulletCount_ = 20;
	float telegraphTime_ = 1.0f;
	int   ringCount_ = 2;
	float ringInterval_ = 0.4f;
	float recoverTime_ = 1.0f;
};
