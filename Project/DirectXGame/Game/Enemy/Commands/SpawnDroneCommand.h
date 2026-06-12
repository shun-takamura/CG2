#pragma once
#include <memory>
#include "Components/Gameplay.h"
#include "Enemy/IEnemyCommand.h"
#include "Enemy/EnemyContext.h"
#include "Enemy/EnemyController.h"
#include "Commands/WanderInScreenCommand.h"
#include "Commands/RetreatCommand.h"
#include "IImGuiEditable.h"
#include "Scene/GameScene.h"

/// <summary>
/// 運び屋（Carrier）用：一定間隔で子敵を生成する。
/// 子敵は運び屋の位置から出現し、徘徊→寿命到達で退避するコマンド列が割り当てられる。
/// 子敵の寿命・徘徊半径・移動速度は親運び屋の CarrierParams から取得する。
/// </summary>
class SpawnDroneCommand : public IEnemyCommand {
public:
	void OnEnter(IImGuiEditable*, EnemyContext&) override {
		timer_      = 0.0f;
		spawnCount_ = 0;
	}

	void Update(float dt, IImGuiEditable* entity, EnemyContext& ctx) override {
		if (!ctx.scene || ctx.childPrefab.empty()) return;
		if (spawnCount_ >= ctx.spawnLimit) return;

		timer_ += dt;
		if (timer_ < ctx.spawnIntervalSec) return;
		timer_ -= ctx.spawnIntervalSec;

		// 運び屋の現在位置にスポーン
		Vector3 spawnPos{ 0.0f, 0.0f, 0.0f };
		if (entity) {
			if (const Vector3* p = entity->GetEditableTranslate()) spawnPos = *p;
		}
		IImGuiEditable* child = ctx.scene->SpawnEnemyAt(ctx.childPrefab, spawnPos);
		if (!child) return;
		spawnCount_++;

		// CarrierParams から子のパラメータを取得（運び屋 = entity）
		float lifetime = 10.0f, radius = 8.0f, speed = 5.0f;
		if (entity) {
			const CarrierParams& cp = Gameplay::Of(entity).GetCarrierParams();
			if (cp.enabled) {
				lifetime = cp.childLifetimeSec;
				radius   = cp.childWanderRadius;
				speed    = cp.childMoveSpeed;
			}
		}

		// 子敵専用 EnemyController を作成し、Wander → Retreat のコマンド列を割り当てる
		auto ctrl = std::make_unique<EnemyController>();
		ctrl->entity_           = child;
		ctrl->waveEntryIndex_   = -1;
		ctrl->billboardToPlayer_ = true;
		ctrl->triggerT_         = ctx.railT;        // 子の出現時刻 = 親の現在t
		ctrl->shootIntervalT_   = ctx.shootIntervalT; // 親の射撃間隔をそのまま継承
		std::vector<std::unique_ptr<IEnemyCommand>> cmds;
		cmds.push_back(std::make_unique<WanderInScreenCommand>(entity, radius, lifetime, speed));
		cmds.push_back(std::make_unique<RetreatCommand>());
		ctrl->Init(std::move(cmds));
		ctx.scene->RegisterEnemyController(std::move(ctrl));
	}

	bool IsFinished() const override { return false; }

private:
	float timer_      = 0.0f;
	int   spawnCount_ = 0;
};
