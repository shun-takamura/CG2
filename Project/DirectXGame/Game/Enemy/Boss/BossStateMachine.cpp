#include "Enemy/Boss/BossStateMachine.h"
#include "Enemy/Boss/BossIdleState.h"

BossStateMachine::BossStateMachine()
	: current_(std::make_unique<BossIdleState>()) {}

BossStateMachine::~BossStateMachine() = default;

void BossStateMachine::Update(float dt, BossStateContext& ctx) {
	// 共有データを状態へ渡し、クールタイムを減らす
	ctx.brain = &brain_;
	if (brain_.attackCooldown > 0.0f) brain_.attackCooldown -= dt;
	if (brain_.jumpCooldown   > 0.0f) brain_.jumpCooldown   -= dt;

	// 部位破壊は外からの出来事。今の状態に知らせ、移る先（既定はひるみ）を状態に決めさせる
	if (staggerRequest_ >= 0.0f) {
		const float seconds = staggerRequest_;
		staggerRequest_ = -1.0f;
		if (auto next = current_->OnPartBroken(seconds, ctx)) ChangeState(std::move(next), ctx);
	}

	if (auto next = current_->Update(dt, ctx)) ChangeState(std::move(next), ctx);
}

void BossStateMachine::ChangeState(std::unique_ptr<IBossState> next, BossStateContext& ctx) {
	current_->OnExit(ctx);
	current_ = std::move(next);
	current_->OnEnter(ctx);
}
