#pragma once
#include "Enemy/IEnemyCommand.h"
#include "Enemy/EnemyContext.h"
#include "IImGuiEditable.h"
#include "Scene/BaseScene.h"

/// <summary>
/// 運び屋コマンド：一定間隔（秒）でザコをスポーンし続ける。
/// IsFinished() は常に false（外部から TriggerRetreat で中断する）。
/// </summary>
class SpawnDroneCommand : public IEnemyCommand {
public:
	void OnEnter(IImGuiEditable*, EnemyContext& ctx) override {
		timer_      = 0.0f;
		spawnCount_ = 0;
	}

	void Update(float dt, IImGuiEditable* entity, EnemyContext& ctx) override {
		if (!ctx.scene || ctx.childPrefab.empty()) return;
		if (spawnCount_ >= ctx.spawnLimit) return;

		timer_ += dt;
		if (timer_ >= ctx.spawnIntervalSec) {
			timer_ -= ctx.spawnIntervalSec;

			// 運び屋の位置近傍にスポーン
			Vector3 spawnPos{ 0.0f, 0.0f, 0.0f };
			if (entity) {
				if (const Vector3* p = entity->GetEditableTranslate()) spawnPos = *p;
			}
			// spline がある場合はそちらに乗せる、なければ固定位置
			if (!ctx.childSplineId.empty()) {
				auto* sp = ctx.scene->FindDynamicSplineByName(ctx.childSplineId);
				if (sp) {
					ctx.scene->SpawnEnemyOnSpline(ctx.childPrefab, sp, 0.15f, true, 0.0f, -1);
					spawnCount_++;
				}
			} else {
				// 固定位置スポーン（EnemyController なし、MovingEnemy なし）
				ctx.scene->SpawnEnemyAt(ctx.childPrefab, spawnPos);
				spawnCount_++;
			}
		}
	}

	bool IsFinished() const override { return false; }

private:
	float timer_      = 0.0f;
	int   spawnCount_ = 0;
};
