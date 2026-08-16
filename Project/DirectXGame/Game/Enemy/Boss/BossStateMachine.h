#pragma once
#include <random>
#include "Enemy/Boss/BossActionManager.h"

/// <summary>
/// ボスの行動パターンを選ぶ本体。攻撃/移動をクールタイム＋重み付き抽選で切り替え、
/// 一定リズムでの来襲にならないようにする。
/// </summary>
class BossStateMachine {
public:
	void Update(float dt, BossActionContext& ctx);

private:
	void SelectNext(BossActionContext& ctx);

	BossActionManager manager_;
	std::mt19937 rng_{ std::random_device{}() };

	float attackCooldown_     = 0.0f; // 攻撃終了後、次に攻撃を選べるまでの残り秒
	float attackCooldownBase_ = 1.5f; // 攻撃選択のたびにこの値へ再セット
	float attackChance_       = 0.5f; // クールタイム明け時、攻撃を選ぶ確率（残りは移動）
};
