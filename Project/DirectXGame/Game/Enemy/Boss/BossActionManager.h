#pragma once
#include <memory>
#include "Enemy/Boss/IBossAction.h"

/// <summary>
/// 現在実行中の IBossAction を1つだけ保持して駆動する受け渡し役。
/// 判定・フラグ・行動選択ロジックは持たない（選ぶのは BossStateMachine の役目）。
/// </summary>
class BossActionManager {
public:
	void Start(std::unique_ptr<IBossAction> action, BossActionContext& ctx);
	void Update(float dt, BossActionContext& ctx);
	bool IsIdle() const { return current_ == nullptr; }

private:
	std::unique_ptr<IBossAction> current_;
};
