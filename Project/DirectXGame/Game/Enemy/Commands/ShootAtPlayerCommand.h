#pragma once
#include <cmath>
#include "Enemy/IEnemyCommand.h"
#include "Enemy/EnemyContext.h"
#include "IImGuiEditable.h"
#include "Scene/GameScene.h"
#include "Components/PrefabManager.h"

/// <summary>
/// 一定間隔（秒）でプレイヤー方向に敵弾を発射する。
/// IsFinished() は常に false（外部から TriggerRetreat で中断する）。
/// </summary>
class ShootAtPlayerCommand : public IEnemyCommand {
public:
	void Update(float dt, IImGuiEditable* entity, EnemyContext& ctx) override {
		if (!ctx.player || !entity || !ctx.scene) return;
		if (ctx.shootIntervalSec <= 0.0f) return;

		const float secSinceSpawn = ctx.stageTimeSec - ctx.triggerSec;
		if (secSinceSpawn < 0.0f) return;

		const int shotIdx = static_cast<int>(secSinceSpawn / ctx.shootIntervalSec);
		if (shotIdx > lastShotIdx_) {
			lastShotIdx_ = shotIdx;
			// 画面外から撃たれると理不尽なので、画面内にいる時だけ発射する。
			// 発射をスキップしても shotIdx は進めるので、画面内に入った瞬間に
			// 溜まっていた分を連射することはない。
			if (const Vector3* pos = entity->GetEditableTranslate()) {
				if (!ctx.CanAttackFrom(*pos)) return;
			}
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

		// 偏差射撃：着弾までの時間ぶん先の位置を狙う。レールの前進は完全に、
		// プレイヤー自身の移動は shotLeadRate だけ先読みする（動き続ければ避けられ、止まると当たる）。
		float bulletSpeed = 40.0f;
		if (const PrefabDef* def = PrefabManager::GetInstance()->Find("EnemyBullet"); def && def->hasBullet && def->bulletSpeed > 1e-3f) {
			bulletSpeed = def->bulletSpeed;
		}
		const Vector3 leadVel{
			ctx.playerRailVelocity.x + ctx.playerOwnVelocity.x * ctx.shotLeadRate,
			ctx.playerRailVelocity.y + ctx.playerOwnVelocity.y * ctx.shotLeadRate,
			ctx.playerRailVelocity.z + ctx.playerOwnVelocity.z * ctx.shotLeadRate };
		Vector3 aim = *ppos;
		for (int i = 0; i < 2; ++i) { // 着弾時間→予測位置を2回反復して収束させる
			const float dx = aim.x - pos->x, dy = aim.y - pos->y, dz = aim.z - pos->z;
			const float t = std::sqrt(dx * dx + dy * dy + dz * dz) / bulletSpeed;
			aim = { ppos->x + leadVel.x * t, ppos->y + leadVel.y * t, ppos->z + leadVel.z * t };
		}

		Vector3 dir{ aim.x - pos->x, aim.y - pos->y, aim.z - pos->z };
		const float len = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
		if (len < 0.01f) return;
		dir = { dir.x / len, dir.y / len, dir.z / len };

		// プレイヤーを homingTarget として渡す（ホーミング強度はプレハブから）
		ctx.scene->SpawnEnemyBullet(*pos, dir, -1.0f, -1.0f, "EnemyBullet", ctx.player);
	}
};
