#pragma once
#include <cmath>
#include "Enemy/Boss/IBossAction.h"
#include "Scene/GameScene.h"
#include "IImGuiEditable.h"

/// <summary>
/// 遠距離攻撃：予兆→プレイヤー狙いの弾を1発発射→硬直、で終了する（次の行動は BossStateMachine が選び直す）。
/// 既存 BossAttackCommand（雑魚パイプ側・縦スライス版）の中身を移植したもの。
/// </summary>
class BossAttackRanged : public IBossAction {
	enum class Phase { Telegraph, Active, Recover, Done };

public:
	explicit BossAttackRanged(float telegraphTime = 1.2f, float recoverTime = 1.0f)
		: telegraphTime_(telegraphTime), recoverTime_(recoverTime) {}

	void OnEnter(BossActionContext& ctx) override {
		phase_ = Phase::Telegraph;
		timer_ = 0.0f;
		ctx.billboardToPlayer = true;
	}

	void Update(float dt, BossActionContext& ctx) override {
		ctx.billboardToPlayer = true;
		timer_ += dt;

		if (phase_ == Phase::Telegraph) {
			// 予兆（攻撃なし）。将来ここで telegraph エフェクトを鳴らす。
			if (timer_ >= telegraphTime_) {
				FireAtPlayer(ctx);
				phase_ = Phase::Active;
				timer_ = 0.0f;
			}
		} else if (phase_ == Phase::Active) {
			phase_ = Phase::Recover;
			timer_ = 0.0f;
		} else if (phase_ == Phase::Recover) {
			if (timer_ >= recoverTime_) phase_ = Phase::Done;
		}
	}

	bool IsFinished() const override { return phase_ == Phase::Done; }

private:
	void FireAtPlayer(BossActionContext& ctx) {
		if (!ctx.boss || !ctx.player || !ctx.scene) return;
		const Vector3* bp = ctx.boss->GetEditableTranslate();
		const Vector3* pp = ctx.player->GetEditableTranslate();
		if (!bp || !pp) return;
		Vector3 d{ pp->x - bp->x, pp->y - bp->y, pp->z - bp->z };
		const float len = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
		if (len < 1e-4f) return;
		d = { d.x / len, d.y / len, d.z / len };
		// 弾速/寿命/ホーミングは EnemyBullet プレハブの bullet セクションから（負数＝プレハブ既定）。
		ctx.scene->SpawnEnemyBullet(*bp, d, -1.0f, -1.0f, "EnemyBullet", ctx.player);
	}

	Phase phase_         = Phase::Telegraph;
	float timer_         = 0.0f;
	float telegraphTime_ = 1.2f;
	float recoverTime_   = 1.0f;
};
