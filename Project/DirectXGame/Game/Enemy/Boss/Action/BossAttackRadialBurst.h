#pragma once
#include "Enemy/Boss/IBossAction.h"
#include "Scene/GameScene.h"

/// <summary>
/// 全方位リング：予兆→360°に N 発を一斉発射→半ピッチずらしてもう1周→硬直。
/// リングの基準角はプレイヤー方向に合わせ、1周目の弾が必ずプレイヤーへ向かうようにする。
/// </summary>
class BossAttackRadialBurst : public IBossAction {
	enum class Phase { Telegraph, Firing, Recover, Done };

public:
	BossAttackRadialBurst(int bulletCount = 20, float telegraphTime = 1.0f)
		: bulletCount_(bulletCount), telegraphTime_(telegraphTime) {}

	void OnEnter(BossActionContext& ctx) override {
		phase_ = Phase::Telegraph;
		timer_ = 0.0f;
		ringsFired_ = 0;
		ctx.billboardToPlayer = true;
	}

	void Update(float dt, BossActionContext& ctx) override {
		ctx.billboardToPlayer = true;
		timer_ += dt;

		if (phase_ == Phase::Telegraph) {
			SetBossTelegraphTint(ctx, { 1.0f, 0.9f, 0.2f }, timer_, telegraphTime_); // 弾幕系=黄 / 近接系=白
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

	bool IsFinished() const override { return phase_ == Phase::Done; }

private:
	void FireRing(BossActionContext& ctx) {
		if (!ctx.scene || bulletCount_ <= 0) return;
		const BossAimInfo aim = ComputeBossAim(ctx);
		if (!aim.valid) return;
		constexpr float kTwoPi = 6.2831853f;
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
