#include <cmath>

#include "Enemy/Boss/BossStateMachine.h"
#include "Enemy/Boss/Action/BossAttackMelee.h"
#include "Enemy/Boss/Action/BossAttackRanged.h"
#include "Enemy/Boss/Action/BossMoveApproach.h"
#include "Enemy/Boss/Action/BossMoveJumpOver.h"
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

	if (canAttack && dist(rng_) < attackChance_) {
		if (IsPlayerInMeleeRange(ctx) && dist(rng_) < meleeAttackChance_) {
			manager_.Start(std::make_unique<BossAttackMelee>(), ctx);
		} else {
			manager_.Start(std::make_unique<BossAttackRanged>(), ctx);
		}
		attackCooldown_ = attackCooldownBase_;
		return;
	}

	if (canJump && dist(rng_) < jumpChance_) {
		manager_.Start(std::make_unique<BossMoveJumpOver>(), ctx);
		jumpCooldown_ = jumpCooldownBase_;
		return;
	}

	manager_.Start(std::make_unique<BossMoveApproach>(), ctx);
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
