#pragma once
#include "Enemy/Boss/IBossAction.h"
#include "Scene/GameScene.h"

/// <summary>
/// 突進：予兆（プレイヤーを向いて溜め）→予兆終了時のプレイヤー位置を突き抜ける直線ダッシュ→硬直。
/// ダッシュ中は "BossMeleeHit"（EnemyAttack の判定プレハブ）を体の前に追従させて接触ダメージにする。
/// 目標は予兆の終わりに一度だけ確定するので、溜めを見てから横に避ければかわせる。
/// </summary>
class BossAttackCharge : public IBossAction {
	enum class Phase { Telegraph, Dash, Recover, Done };

public:
	explicit BossAttackCharge(float telegraphTime = 0.9f)
		: telegraphTime_(telegraphTime) {}

	void OnEnter(BossActionContext& ctx) override {
		phase_ = Phase::Telegraph;
		timer_ = 0.0f;
		ctx.billboardToPlayer = true;
	}

	void Update(float dt, BossActionContext& ctx) override {
		timer_ += dt;

		if (phase_ == Phase::Telegraph) {
			SetBossTelegraphTint(ctx, { 1.0f, 1.0f, 1.0f }, timer_, telegraphTime_); // 弾幕系=黄 / 近接系=白
			ctx.billboardToPlayer = true;
			if (timer_ >= telegraphTime_) {
				if (!BeginDash(ctx)) { phase_ = Phase::Done; return; }
				phase_ = Phase::Dash;
				timer_ = 0.0f;
			}
		} else if (phase_ == Phase::Dash) {
			// 突進中は向きを固定（振り向かない）
			ctx.billboardToPlayer = false;
			float t = (dashDuration_ > 1e-4f) ? (timer_ / dashDuration_) : 1.0f;
			if (t > 1.0f) t = 1.0f;
			const float e = t * t * (3.0f - 2.0f * t);
			if (Vector3* bp = ctx.boss ? ctx.boss->GetEditableTranslate() : nullptr) {
				bp->x = startPos_.x + (goalPos_.x - startPos_.x) * e;
				bp->z = startPos_.z + (goalPos_.z - startPos_.z) * e;
			}
			if (hitVolume_) {
				if (Vector3* hp = hitVolume_->GetEditableTranslate()) {
					*hp = ResolveBossHitAnchor(ctx, hitLocalOffset_);
				}
			}
			if (t >= 1.0f) {
				DestroyHitVolume(ctx);
				phase_ = Phase::Recover;
				timer_ = 0.0f;
			}
		} else if (phase_ == Phase::Recover) {
			ctx.billboardToPlayer = true;
			if (timer_ >= recoverTime_) phase_ = Phase::Done;
		}
	}

	void OnExit(BossActionContext& ctx) override {
		DestroyHitVolume(ctx);
	}

	bool IsFinished() const override { return phase_ == Phase::Done; }

private:
	bool BeginDash(BossActionContext& ctx) {
		const BossAimInfo aim = ComputeBossAim(ctx);
		if (!aim.valid || !ctx.scene) return false;
		startPos_ = aim.origin;
		// プレイヤーの位置を overshoot_ だけ突き抜ける
		const float len = aim.distXZ + overshoot_;
		goalPos_ = {
			startPos_.x + std::sin(aim.yaw) * len,
			startPos_.y,
			startPos_.z + std::cos(aim.yaw) * len,
		};
		ClampToBossArena(ctx, goalPos_);
		hitVolume_ = ctx.scene->SpawnEnemyAt("BossMeleeHit", ResolveBossHitAnchor(ctx, hitLocalOffset_));
		if (hitVolume_) hitVolume_->SetRotate({ 0.0f, aim.yaw, 0.0f });
		return true;
	}

	void DestroyHitVolume(BossActionContext& ctx) {
		if (!hitVolume_) return;
		if (ctx.scene) ctx.scene->DestroyDynamicEntity(hitVolume_);
		hitVolume_ = nullptr;
	}

	Phase phase_ = Phase::Telegraph;
	float timer_ = 0.0f;

	float telegraphTime_ = 0.9f;
	float dashDuration_ = 0.6f;
	float overshoot_ = 6.0f;          // プレイヤー位置からさらに進む距離 [m]
	float recoverTime_ = 0.8f;
	Vector3 hitLocalOffset_{ 0.0f, 0.0f, 1.5f };

	Vector3 startPos_{ 0.0f, 0.0f, 0.0f };
	Vector3 goalPos_{ 0.0f, 0.0f, 0.0f };
	IImGuiEditable* hitVolume_ = nullptr;
};
