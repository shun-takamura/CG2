#include <cmath>

#include "Enemy/Boss/BossStateMachine.h"
#include "Enemy/Boss/Action/BossAttackMelee.h"
#include "Enemy/Boss/Action/BossAttackRanged.h"
#include "Enemy/Boss/Action/BossMoveApproach.h"
#include "Enemy/Boss/Action/BossMoveJumpOver.h"
#include "Enemy/Boss/Action/BossAttackSpread.h"
#include "Enemy/Boss/Action/BossAttackRadialBurst.h"
#include "Enemy/Boss/Action/BossAttackCharge.h"
#include "Enemy/Boss/Action/BossAttackSlam.h"
#include "Components/Gameplay.h"
#include "IImGuiEditable.h"

void BossStateMachine::Update(float dt, BossActionContext& ctx) {
	if (attackCooldown_ > 0.0f) attackCooldown_ -= dt;
	if (jumpCooldown_   > 0.0f) jumpCooldown_   -= dt;
	manager_.Update(dt, ctx);
	if (manager_.IsIdle()) SelectNext(ctx);
}

void BossStateMachine::SelectNext(BossActionContext& ctx) {
	const bool canAttack = (attackCooldown_ <= 0.0f);
	const bool canJump   = (jumpCooldown_   <= 0.0f);
	std::uniform_real_distribution<float> dist(0.0f, 1.0f);

	const bool rage = IsRage(ctx);
	if (canAttack && dist(rng_) < (rage ? rageAttackChance_ : attackChance_)) {
		StartAttack(ctx, rage);
		attackCooldown_ = rage ? rageAttackCooldown_ : attackCooldownBase_;
		return;
	}

	if (canJump && dist(rng_) < jumpChance_) {
		manager_.Start(std::make_unique<BossMoveJumpOver>(), ctx);
		jumpCooldown_ = jumpCooldownBase_;
		return;
	}

	manager_.Start(std::make_unique<BossMoveApproach>(), ctx);
}

void BossStateMachine::StartAttack(BossActionContext& ctx, bool rage) {
	enum Kind { kMelee, kSlam, kRadial, kCharge, kSpread, kRanged, kKindCount };
	// 重み付き抽選。近接レンジ内は近距離技中心、レンジ外は弾幕中心。
	static constexpr float kNearWeights[kKindCount] = { 35.0f, 20.0f, 25.0f, 20.0f, 0.0f, 0.0f };
	static constexpr float kFarWeights[kKindCount]  = { 0.0f, 15.0f, 20.0f, 15.0f, 30.0f, 20.0f };
	const float* weights = IsPlayerInMeleeRange(ctx) ? kNearWeights : kFarWeights;

	float total = 0.0f;
	for (int i = 0; i < kKindCount; ++i) total += weights[i];
	std::uniform_real_distribution<float> dist(0.0f, total);
	float r = dist(rng_);
	int kind = kRanged;
	for (int i = 0; i < kKindCount; ++i) {
		if (r < weights[i]) { kind = i; break; }
		r -= weights[i];
	}

	const float tele = rage ? rageTelegraphScale_ : 1.0f;
	switch (kind) {
	case kMelee:  manager_.Start(std::make_unique<BossAttackMelee>(0.6f * tele), ctx); break;
	case kSlam:   manager_.Start(std::make_unique<BossAttackSlam>(rage ? 32 : 24, 0.5f * tele), ctx); break;
	case kRadial: manager_.Start(std::make_unique<BossAttackRadialBurst>(rage ? 28 : 20, 1.0f * tele), ctx); break;
	case kCharge: manager_.Start(std::make_unique<BossAttackCharge>(0.9f * tele), ctx); break;
	case kSpread: manager_.Start(std::make_unique<BossAttackSpread>(rage ? 9 : 7, 0.8f * tele), ctx); break;
	default:      manager_.Start(std::make_unique<BossAttackRanged>(1.2f * tele), ctx); break;
	}
}

bool BossStateMachine::IsRage(const BossActionContext& ctx) const {
	if (!ctx.boss) return false;
	const HP& hp = Gameplay::Of(ctx.boss).GetHP();
	if (!hp.enabled || hp.maxHP <= 0) return false;
	return static_cast<float>(hp.currentHP) <= static_cast<float>(hp.maxHP) * rageHpRatio_;
}

bool BossStateMachine::IsPlayerInMeleeRange(const BossActionContext& ctx) const {
	if (!ctx.boss || !ctx.player) return false;
	const Vector3* bp = ctx.boss->GetEditableTranslate();
	const Vector3* pp = ctx.player->GetEditableTranslate();
	if (!bp || !pp) return false;
	const float dx = pp->x - bp->x;
	const float dz = pp->z - bp->z;
	return (dx * dx + dz * dz) <= meleeRange_ * meleeRange_;
}
