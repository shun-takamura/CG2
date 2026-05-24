#pragma once
#include <cmath>
#include "Enemy/IEnemyCommand.h"
#include "Enemy/EnemyContext.h"
#include "IImGuiEditable.h"

/// <summary>
/// 突進型の溜め→突進コマンド。
///   Phase::Charge : chargeTime_ 秒待機、プレイヤー方向を向く、コライダー OFF
///   Phase::Rush   : 溜め完了時の方向へ高速直線移動、コライダー ON（ジャスト回避対象）
/// スプライン終端到達後（EnemyController に渡されてから）すぐ OnEnter が呼ばれる前提。
/// </summary>
class ChargeRushCommand : public IEnemyCommand {
	enum class Phase { Approach, Charge, Rush, Done };

public:
	explicit ChargeRushCommand(float chargeTime = 1.5f, float rushSpeed = 55.0f,
		float rushMaxDistance = 120.0f)
		: chargeTime_(chargeTime), rushSpeed_(rushSpeed), rushMaxDistance_(rushMaxDistance) {}

	void OnEnter(IImGuiEditable* entity, EnemyContext& ctx) override {
		phase_        = Phase::Approach;
		chargeTimer_  = 0.0f;
		rushTraveled_ = 0.0f;
		// Approach 中はスプライン追従に任せる（requestDetach しない）
		ctx.billboardToPlayer = true;
		// 溜め前は当たり判定 OFF（接触ダメージなし）
		if (entity) entity->GetCollider().enabled = false;
	}

	void Update(float dt, IImGuiEditable* entity, EnemyContext& ctx) override {
		// Approach: スプライン終端に到達するまで待機
		if (phase_ == Phase::Approach) {
			ctx.billboardToPlayer = true;
			if (ctx.splineArrived) {
				phase_ = Phase::Charge;
				chargeTimer_ = 0.0f;
				ctx.requestDetach = true; // 以降は自由移動
			}
			return;
		}
		if (phase_ == Phase::Charge) {
			ctx.billboardToPlayer = true;
			// 完了時の突進方向をプレイヤー方向で固定
			if (entity && ctx.player) {
				Vector3* pos  = entity->GetEditableTranslate();
				Vector3* ppos = ctx.player->GetEditableTranslate();
				if (pos && ppos) {
					Vector3 d{ ppos->x - pos->x, ppos->y - pos->y, ppos->z - pos->z };
					const float len = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
					if (len > 0.01f) rushDir_ = { d.x / len, d.y / len, d.z / len };
				}
			}
			chargeTimer_ += dt;
			if (chargeTimer_ >= chargeTime_) {
				phase_ = Phase::Rush;
				ctx.billboardToPlayer = false;
				if (entity) entity->GetCollider().enabled = true; // 突進中は当たり判定 ON
				ctx.useFreeVelocity = true;
				ctx.freeVelocity = {
					rushDir_.x * rushSpeed_, rushDir_.y * rushSpeed_, rushDir_.z * rushSpeed_ };
			}
		} else if (phase_ == Phase::Rush) {
			ctx.billboardToPlayer = false;
			ctx.useFreeVelocity   = true;
			ctx.contactDamageActive = true; // 突進中のみ接触ダメージ（ジャスト回避対象）
			ctx.freeVelocity      = {
				rushDir_.x * rushSpeed_, rushDir_.y * rushSpeed_, rushDir_.z * rushSpeed_ };
			rushTraveled_ += rushSpeed_ * dt;
			if (rushTraveled_ >= rushMaxDistance_) phase_ = Phase::Done;
		}
	}

	void OnExit(IImGuiEditable* entity, EnemyContext& ctx) override {
		if (entity) entity->GetCollider().enabled = false;
	}

	bool IsFinished() const override { return phase_ == Phase::Done; }

private:
	Phase   phase_          = Phase::Approach;
	float   chargeTime_     = 1.5f;
	float   chargeTimer_    = 0.0f;
	float   rushSpeed_      = 55.0f;
	float   rushMaxDistance_ = 120.0f;
	float   rushTraveled_   = 0.0f;
	Vector3 rushDir_{ 0.0f, 0.0f, 1.0f };
};
