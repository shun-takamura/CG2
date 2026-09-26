#pragma once
#include "Enemy/Boss/IBossAction.h"
#include "Scene/GameScene.h"

/// <summary>
/// 扇状弾：予兆→プレイヤー方向へ扇形に N 発、を角度を半ピッチずつずらして数回連射→硬直。
/// 各斉射の直前に照準を取り直すので、横移動し続けないと隙間に入れない。
/// </summary>
class BossAttackSpread : public IBossAction {
	enum class Phase { Telegraph, Firing, Recover, Done };

public:
	BossAttackSpread(int bulletCount = 7, float telegraphTime = 0.8f)
		: bulletCount_(bulletCount), telegraphTime_(telegraphTime) {}

	void OnEnter(BossActionContext& ctx) override {
		phase_ = Phase::Telegraph;
		timer_ = 0.0f;
		volleysFired_ = 0;
		ctx.billboardToPlayer = true;
	}

	void Update(float dt, BossActionContext& ctx) override {
		ctx.billboardToPlayer = true;
		timer_ += dt;

		if (phase_ == Phase::Telegraph) {
			if (timer_ >= telegraphTime_) {
				phase_ = Phase::Firing;
				timer_ = volleyInterval_; // 1斉射目を即発射
			}
		} else if (phase_ == Phase::Firing) {
			if (timer_ >= volleyInterval_) {
				timer_ = 0.0f;
				FireVolley(ctx);
				if (++volleysFired_ >= volleyCount_) phase_ = Phase::Recover;
			}
		} else if (phase_ == Phase::Recover) {
			if (timer_ >= recoverTime_) phase_ = Phase::Done;
		}
	}

	bool IsFinished() const override { return phase_ == Phase::Done; }

private:
	void FireVolley(BossActionContext& ctx) {
		if (!ctx.scene || bulletCount_ <= 0) return;
		const BossAimInfo aim = ComputeBossAim(ctx);
		if (!aim.valid) return;
		const float step = (bulletCount_ > 1) ? spreadAngle_ / static_cast<float>(bulletCount_ - 1) : 0.0f;
		const float start = -spreadAngle_ * 0.5f + ((volleysFired_ % 2 == 1) ? step * 0.5f : 0.0f);
		for (int i = 0; i < bulletCount_; ++i) {
			const Vector3 dir = BossDirFromYawPitch(aim.yaw + start + step * static_cast<float>(i), aim.pitch);
			ctx.scene->SpawnEnemyBullet(aim.origin, dir, -1.0f, -1.0f, "EnemyBullet", nullptr, 0.0f);
		}
	}

	Phase phase_ = Phase::Telegraph;
	float timer_ = 0.0f;
	int   volleysFired_ = 0;

	int   bulletCount_ = 7;
	float telegraphTime_ = 0.8f;
	float spreadAngle_ = 1.047f;   // 扇の全幅 [rad]（60°）
	int   volleyCount_ = 3;
	float volleyInterval_ = 0.35f;
	float recoverTime_ = 0.8f;
};
