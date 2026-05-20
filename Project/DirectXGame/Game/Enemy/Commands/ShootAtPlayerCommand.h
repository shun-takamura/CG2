#pragma once
#include <cmath>
#include "Enemy/IEnemyCommand.h"
#include "Enemy/EnemyContext.h"
#include "IImGuiEditable.h"
#include "Scene/BaseScene.h"

/// <summary>
/// カメラ進行度 t の一定間隔でプレイヤー方向に敵弾を発射する。
/// IsFinished() は常に false（外部から TriggerRetreat で中断する）。
/// </summary>
class ShootAtPlayerCommand : public IEnemyCommand {
public:
	void Update(float dt, IImGuiEditable* entity, EnemyContext& ctx) override {
		if (!ctx.player || !entity || !ctx.scene) return;
		if (ctx.shootIntervalT <= 0.0f) return;

		const float tSinceSpawn = ctx.railT - ctx.triggerT;
		if (tSinceSpawn < 0.0f) return;

		const int shotIdx = static_cast<int>(tSinceSpawn / ctx.shootIntervalT);
		if (shotIdx > lastShotIdx_) {
			lastShotIdx_ = shotIdx;
			Fire(entity, ctx);
		}
	}

	bool IsFinished() const override { return false; }

private:
	int lastShotIdx_ = -1;

	void Fire(IImGuiEditable* entity, EnemyContext& ctx) {
		Vector3* pos   = entity->GetEditableTranslate();
		Vector3* ppos  = ctx.player->GetEditableTranslate();
		if (!pos || !ppos) return;

		Vector3 dir{ ppos->x - pos->x, ppos->y - pos->y, ppos->z - pos->z };
		const float len = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
		if (len < 0.01f) return;
		dir = { dir.x / len, dir.y / len, dir.z / len };

		ctx.scene->SpawnEnemyBullet(*pos, dir);
	}
};
