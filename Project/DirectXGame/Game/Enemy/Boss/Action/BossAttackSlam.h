#pragma once
#include "Enemy/Boss/IBossAction.h"
#include "Scene/GameScene.h"

/// <summary>
/// 叩きつけ：沈み込み（予兆）→プレイヤーの手前へ高く跳ぶ→着地の瞬間に地面すれすれの衝撃波リング（N 発）→硬直。
/// 着地点は跳ぶ瞬間に一度だけ確定する（空中で追尾しない＝着地点を見て離れればかわせる）。
/// プレイヤーの真上に着地すると衝撃波が発生と同時に当たって理不尽なので、手前 landShortDistance_ に降りる。
/// </summary>
class BossAttackSlam : public IBossAction {
	enum class Phase { Crouch, Jump, Recover, Done };

public:
	BossAttackSlam(int waveBulletCount = 24, float crouchTime = 0.5f)
		: waveBulletCount_(waveBulletCount), crouchTime_(crouchTime) {}

	void OnEnter(BossActionContext& ctx) override {
		phase_ = Phase::Crouch;
		timer_ = 0.0f;
		ctx.billboardToPlayer = true;
		if (const Vector3* bp = ctx.boss ? ctx.boss->GetEditableTranslate() : nullptr) {
			baselineY_ = bp->y;
		}
	}

	void Update(float dt, BossActionContext& ctx) override {
		ctx.billboardToPlayer = true;
		timer_ += dt;
		Vector3* bp = ctx.boss ? ctx.boss->GetEditableTranslate() : nullptr;
		if (!bp) { phase_ = Phase::Done; return; }

		if (phase_ == Phase::Crouch) {
			float t = (crouchTime_ > 1e-4f) ? (timer_ / crouchTime_) : 1.0f;
			if (t > 1.0f) t = 1.0f;
			bp->y = baselineY_ - crouchDepth_ * std::sin(3.14159265f * 0.5f * t);
			if (t >= 1.0f) {
				if (!BeginJump(ctx)) { bp->y = baselineY_; phase_ = Phase::Done; return; }
				phase_ = Phase::Jump;
				timer_ = 0.0f;
			}
		} else if (phase_ == Phase::Jump) {
			float t = (jumpDuration_ > 1e-4f) ? (timer_ / jumpDuration_) : 1.0f;
			if (t > 1.0f) t = 1.0f;
			bp->x = startPos_.x + (goalPos_.x - startPos_.x) * t;
			bp->z = startPos_.z + (goalPos_.z - startPos_.z) * t;
			// 沈んだ位置から跳び、元の高さに着地する放物線
			const float base = (baselineY_ - crouchDepth_) + crouchDepth_ * t;
			bp->y = base + jumpHeight_ * std::sin(3.14159265f * t);
			if (t >= 1.0f) {
				bp->y = baselineY_;
				FireShockwave(ctx, *bp);
				phase_ = Phase::Recover;
				timer_ = 0.0f;
			}
		} else if (phase_ == Phase::Recover) {
			if (timer_ >= recoverTime_) phase_ = Phase::Done;
		}
	}

	void OnExit(BossActionContext& ctx) override {
		// 途中中断でも沈み込み/空中の高さを残さない
		if (phase_ != Phase::Recover && phase_ != Phase::Done) {
			if (Vector3* bp = ctx.boss ? ctx.boss->GetEditableTranslate() : nullptr) bp->y = baselineY_;
		}
	}

	bool IsFinished() const override { return phase_ == Phase::Done; }

private:
	bool BeginJump(BossActionContext& ctx) {
		const BossAimInfo aim = ComputeBossAim(ctx);
		if (!aim.valid) return false;
		startPos_ = aim.origin;
		const float len = (std::max)(0.0f, aim.distXZ - landShortDistance_);
		goalPos_ = {
			startPos_.x + std::sin(aim.yaw) * len,
			baselineY_,
			startPos_.z + std::cos(aim.yaw) * len,
		};
		// 衝撃波はプレイヤーの胸の高さを水平に走らせる（地面すれすれ＝地上のプレイヤーに当たる高さ）
		const Vector3* pp = ctx.player ? ctx.player->GetEditableTranslate() : nullptr;
		shockwaveY_ = pp ? pp->y + shockwaveHeight_ : baselineY_;
		return true;
	}

	void FireShockwave(BossActionContext& ctx, const Vector3& landPos) {
		if (!ctx.scene || waveBulletCount_ <= 0) return;
		constexpr float kTwoPi = 6.2831853f;
		const Vector3 origin{ landPos.x, shockwaveY_, landPos.z };
		const float step = kTwoPi / static_cast<float>(waveBulletCount_);
		for (int i = 0; i < waveBulletCount_; ++i) {
			const Vector3 dir = BossDirFromYawPitch(step * static_cast<float>(i), 0.0f);
			ctx.scene->SpawnEnemyBullet(origin, dir, shockwaveSpeed_, -1.0f, "EnemyBullet", nullptr, 0.0f);
		}
	}

	Phase phase_ = Phase::Crouch;
	float timer_ = 0.0f;

	int   waveBulletCount_ = 24;
	float crouchTime_ = 0.5f;
	float crouchDepth_ = 0.6f;
	float jumpDuration_ = 0.9f;
	float jumpHeight_ = 10.0f;
	float landShortDistance_ = 5.0f;  // プレイヤーの手前に降りる距離 [m]
	float shockwaveHeight_ = 1.0f;    // プレイヤー足元からの衝撃波の高さ [m]
	float shockwaveSpeed_ = 25.0f;    // 衝撃波は通常弾より遅く、見て避けられる速さ
	float recoverTime_ = 1.2f;

	float baselineY_ = 0.0f;
	float shockwaveY_ = 0.0f;
	Vector3 startPos_{ 0.0f, 0.0f, 0.0f };
	Vector3 goalPos_{ 0.0f, 0.0f, 0.0f };
};
