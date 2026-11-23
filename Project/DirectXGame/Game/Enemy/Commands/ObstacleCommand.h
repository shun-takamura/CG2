#pragma once
#include "Enemy/Commands/IEnemyCommand.h"
#include "Enemy/EnemyContext.h"

/// <summary>
/// 谷の障害物（とがった岩・石柱の梁。4_StageGimmick.md）。動かない・撃たない・退避しない。
/// 触れると常に接触ダメージ（＝ジャスト回避の対象）。向きは Wave の rotate_deg のまま変えない。
/// 壊されるまで居座る（IsFinished は常に false）。
/// </summary>
class ObstacleCommand : public IEnemyCommand {
public:
	/// <summary>自機の方を向かないようにする（Wave の向きのまま）</summary>
	/// <param name="entity">動かす敵（このコマンドでは使わない）</param>
	/// <param name="ctx">敵の入力と結果の書き込み先</param>
	void OnEnter([[maybe_unused]] IImGuiEditable* entity, EnemyContext& ctx) override {
		ctx.billboardToPlayer = false;
	}

	/// <summary>毎フレーム接触ダメージを有効にし、向きを固定する</summary>
	/// <param name="ctx">敵の入力と結果の書き込み先</param>
	void Update(float, IImGuiEditable*, EnemyContext& ctx) override {
		ctx.billboardToPlayer   = false;
		ctx.contactDamageActive = true;
	}

	/// <summary>コマンドが終わったか（壊されるまで居座るので終わらない）</summary>
	/// <returns>常に false</returns>
	bool IsFinished() const override { return false; }
};
