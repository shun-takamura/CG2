#include "Enemy/Boss/BossStateMachine.h"
#include "Enemy/Boss/Action/BossAttackRanged.h"
#include "Enemy/Boss/Action/BossMoveApproach.h"

void BossStateMachine::Update(float dt, BossActionContext& ctx) {
	if (attackCooldown_ > 0.0f) attackCooldown_ -= dt;
	manager_.Update(dt, ctx);
	if (manager_.IsIdle()) SelectNext(ctx);
}

void BossStateMachine::SelectNext(BossActionContext& ctx) {
	const bool canAttack = (attackCooldown_ <= 0.0f);
	std::uniform_real_distribution<float> dist(0.0f, 1.0f);

	if (canAttack && dist(rng_) < attackChance_) {
		manager_.Start(std::make_unique<BossAttackRanged>(), ctx);
		attackCooldown_ = attackCooldownBase_;
	} else {
		manager_.Start(std::make_unique<BossMoveApproach>(), ctx);
	}
}
