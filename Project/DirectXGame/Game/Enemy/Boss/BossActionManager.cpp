#include "Enemy/Boss/BossActionManager.h"

void BossActionManager::Start(std::unique_ptr<IBossAction> action, BossActionContext& ctx) {
	if (current_) current_->OnExit(ctx);
	current_ = std::move(action);
	if (current_) current_->OnEnter(ctx);
}

void BossActionManager::Update(float dt, BossActionContext& ctx) {
	if (!current_) return;
	current_->Update(dt, ctx);
	if (current_->IsFinished()) {
		current_->OnExit(ctx);
		current_.reset();
	}
}
