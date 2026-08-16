#pragma once
#include "Enemy/IEnemyCommand.h"
#include "Enemy/EnemyContext.h"
#include "Enemy/Boss/BossStateMachine.h"
#include "IImGuiEditable.h"

/// <summary>
/// EnemyController（雑魚敵と共通の登録／死亡掃除の配管）に積むための薄いアダプタ。
/// 実際の行動選択・実行は BossStateMachine に丸投げする。雑魚敵の IEnemyCommand 一本道の
/// 実行順序には乗らない＝ボスの行動パターンはこのコマンド1つの中で完結する。
/// </summary>
class BossBrainCommand : public IEnemyCommand {
public:
	void Update(float dt, IImGuiEditable* entity, EnemyContext& ctx) override {
		BossActionContext bctx;
		bctx.boss             = entity;
		bctx.player            = ctx.player;
		bctx.scene             = ctx.scene;
		bctx.billboardToPlayer = ctx.billboardToPlayer;

		brain_.Update(dt, bctx);

		ctx.billboardToPlayer = bctx.billboardToPlayer;
	}

	bool IsFinished() const override { return false; } // 撃破されるまでループ

private:
	BossStateMachine brain_;
};
