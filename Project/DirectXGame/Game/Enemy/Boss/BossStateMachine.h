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
	void StartAttack(BossActionContext& ctx, bool rage);
	bool IsPlayerInMeleeRange(const BossActionContext& ctx) const;
	bool IsRage(const BossActionContext& ctx) const;

	BossActionManager manager_;
	std::mt19937 rng_{ std::random_device{}() };

	float attackCooldown_     = 0.0f; // 攻撃終了後、次に攻撃を選べるまでの残り秒
	float attackCooldownBase_ = 1.5f; // 攻撃選択のたびにこの値へ再セット
	float attackChance_       = 0.5f; // クールタイム明け時、攻撃を選ぶ確率（残りは移動系から選ぶ）

	// 発狂（HP 比率がこれ以下）：攻撃間隔を詰め、攻撃頻度・弾数を上げ、予兆を短くする
	float rageHpRatio_           = 0.5f;
	float rageAttackCooldown_    = 0.9f;
	float rageAttackChance_      = 0.7f;
	float rageTelegraphScale_    = 0.8f;

	float meleeRange_       = 9.0f; // このXZ距離以内なら近接攻撃を選択肢に入れる

	float jumpCooldown_       = 0.0f; // ジャンプ飛び越え後、次に選べるまでの残り秒
	float jumpCooldownBase_   = 4.0f; // 連発防止（飛び道具ほど頻発させない）
	float jumpChance_         = 0.3f; // 攻撃不可時、移動系の中でジャンプを選ぶ確率（残りは接近移動）
};
