#pragma once
#include "Enemy/IEnemyCommand.h"
#include "Enemy/EnemyContext.h"

/// <summary>
/// 谷の障害物（とがった岩・石柱の梁。4_StageGimmick.md）。動かない・撃たない・退避しない。
/// 触れると常に接触ダメージ（＝ジャスト回避の対象）。向きは Wave の rotate_deg のまま変えない。
/// 壊されるまで居座る（IsFinished は常に false）。
/// </summary>
class ObstacleCommand : public IEnemyCommand {
public:
	void OnEnter(IImGuiEditable*, EnemyContext& ctx) override {
		ctx.billboardToPlayer = false;
	}

	void Update(float, IImGuiEditable*, EnemyContext& ctx) override {
		ctx.billboardToPlayer   = false;
		ctx.contactDamageActive = true;
	}

	bool IsFinished() const override { return false; }
};
