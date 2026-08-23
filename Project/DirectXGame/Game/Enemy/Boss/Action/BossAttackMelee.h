#pragma once
#include <cmath>
#include "Enemy/Boss/IBossAction.h"
#include "Scene/GameScene.h"
#include "IImGuiEditable.h"

/// <summary>
/// 近接攻撃：予兆→ヒット判定（一定時間持続）→硬直。
/// アニメーション未実装のため、判定は "BossMeleeHit" プレハブ（半透明プリミティブ＋Collider）を
/// ResolveBossHitAnchor（ボス root 基準）の位置に生成し、Collider のワイヤーフレーム表示
/// （CollisionManager::DrawDebug、_DEBUG時のみ）で代用する。
/// アニメ導入後は ResolveBossHitAnchor の中身をボーン基準に差し替えるだけでよく、
/// このクラス自体（タイミング・生成/追従/破棄のロジック）は変更不要。
/// </summary>
class BossAttackMelee : public IBossAction {
	enum class Phase { Telegraph, Active, Recover, Done };

public:
	explicit BossAttackMelee(float telegraphTime = 0.6f, float activeDuration = 0.25f, float recoverTime = 0.5f)
		: telegraphTime_(telegraphTime), activeDuration_(activeDuration), recoverTime_(recoverTime) {}

	void OnEnter(BossActionContext& ctx) override {
		phase_ = Phase::Telegraph;
		timer_ = 0.0f;
		ctx.billboardToPlayer = true;
	}

	void Update(float dt, BossActionContext& ctx) override {
		ctx.billboardToPlayer = true;
		timer_ += dt;

		if (phase_ == Phase::Telegraph) {
			// 予兆（判定なし）。将来ここで振りかぶりエフェクト/アニメを鳴らす。
			if (timer_ >= telegraphTime_) {
				SpawnHitVolume(ctx);
				phase_ = Phase::Active;
				timer_ = 0.0f;
			}
		} else if (phase_ == Phase::Active) {
			UpdateHitVolume(ctx);
			if (timer_ >= activeDuration_) {
				DestroyHitVolume(ctx);
				phase_ = Phase::Recover;
				timer_ = 0.0f;
			}
		} else if (phase_ == Phase::Recover) {
			if (timer_ >= recoverTime_) phase_ = Phase::Done;
		}
	}

	void OnExit(BossActionContext& ctx) override {
		DestroyHitVolume(ctx); // 途中中断でも判定を残さない
	}

	bool IsFinished() const override { return phase_ == Phase::Done; }

private:
	void SpawnHitVolume(BossActionContext& ctx) {
		if (!ctx.scene) return;
		const Vector3 pos = ResolveBossHitAnchor(ctx, hitLocalOffset_);
		hitVolume_ = ctx.scene->SpawnEnemyAt("BossMeleeHit", pos);
		if (hitVolume_) {
			hitVolume_->SetRotate(ComputeFacingRotate(ctx)); // OBB を前方（ボス→プレイヤー方向）へ向ける
		}
	}

	void UpdateHitVolume(BossActionContext& ctx) {
		if (!hitVolume_) return;
		if (Vector3* p = hitVolume_->GetEditableTranslate()) {
			*p = ResolveBossHitAnchor(ctx, hitLocalOffset_);
		}
	}

	void DestroyHitVolume(BossActionContext& ctx) {
		if (!hitVolume_) return;
		if (ctx.scene) ctx.scene->DestroyDynamicEntity(hitVolume_);
		hitVolume_ = nullptr;
	}

	Vector3 ComputeFacingRotate(const BossActionContext& ctx) const {
		Vector3 fwd{ 0.0f, 0.0f, 1.0f };
		if (ctx.boss && ctx.player) {
			const Vector3* bp = ctx.boss->GetEditableTranslate();
			const Vector3* pp = ctx.player->GetEditableTranslate();
			if (bp && pp) {
				Vector3 d{ pp->x - bp->x, 0.0f, pp->z - bp->z };
				const float len = std::sqrt(d.x * d.x + d.z * d.z);
				if (len > 1e-4f) fwd = { d.x / len, 0.0f, d.z / len };
			}
		}
		return { 0.0f, std::atan2(fwd.x, fwd.z), 0.0f };
	}

	Phase phase_          = Phase::Telegraph;
	float timer_          = 0.0f;
	float telegraphTime_  = 0.6f;
	float activeDuration_ = 0.25f;
	float recoverTime_    = 0.5f;
	Vector3 hitLocalOffset_{ 0.0f, 0.0f, 4.0f }; // アンカーからの前方オフセット（間合い）

	IImGuiEditable* hitVolume_ = nullptr;
};
