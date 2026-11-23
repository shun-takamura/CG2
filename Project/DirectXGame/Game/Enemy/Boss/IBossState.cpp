#include "Enemy/Boss/IBossState.h"
#include "Enemy/Boss/BossIdleState.h"
#include "Enemy/Boss/Action/BossStagger.h"

namespace {
	// ひるみの後、すぐには攻撃させない最低のクールタイム [秒]
	constexpr float kAfterStaggerAttackCooldown = 0.6f;
}

std::unique_ptr<IBossState> IBossState::OnPartBroken(float staggerSeconds, BossStateContext& ctx) {
	// 今の行動は打ち切る（OnExit で判定の消去・高さの戻しが走る）
	ctx.brain->attackCooldown = (std::max)(ctx.brain->attackCooldown, kAfterStaggerAttackCooldown);
	return std::make_unique<BossStagger>(staggerSeconds);
}

std::unique_ptr<IBossState> BossActionState::Update(float dt, BossStateContext& ctx) {
	Tick(dt, ctx);
	return IsFinished() ? NextState(ctx) : nullptr;
}

std::unique_ptr<IBossState> BossActionState::NextState(BossStateContext& ctx) {
	(void)ctx;
	return std::make_unique<BossIdleState>();
}
