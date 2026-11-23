#pragma once
#include <cmath>
#include "Enemy/Commands/IEnemyCommand.h"
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
	/// <summary>出現からの経過秒で射撃間隔を数え、間隔ごとに 1 発撃つ（画面外ではその回を飛ばす）</summary>
	/// <param name="dt">経過時間 [秒]（時刻は ctx.stageTimeSec で数えるので使わない）</param>
	/// <param name="entity">敵</param>
	/// <param name="ctx">敵の入力と結果の書き込み先</param>
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

	/// <summary>コマンドが終わったか（TriggerRetreat で外から中断するので終わらない）</summary>
	/// <returns>常に false</returns>
	bool IsFinished() const override { return false; }

private:
	// EnemyBullet プレハブに弾速が無いときの弾速 [m/秒]
	static constexpr float kFallbackBulletSpeed = 40.0f;
	// これより遅い弾速はプレハブに無いものとみなす [m/秒]
	static constexpr float kMinBulletSpeed = 1e-3f;
	// 着弾時間→予測位置を繰り返す回数
	static constexpr int kLeadIterations = 2;
	// これより自機に近いときは撃たない（向きが決まらない）[m]
	static constexpr float kNearZeroLength = 0.01f;

	int lastShotIdx_ = -1;

	/// <summary>自機の当たり判定の中心を偏差射撃で狙い、追尾つきの弾を 1 発撃つ</summary>
	/// <param name="entity">敵</param>
	/// <param name="ctx">敵の入力（自機の速度・先読み率）と結果の書き込み先</param>
	void Fire(IImGuiEditable* entity, EnemyContext& ctx) {
		Vector3* pos   = entity->GetEditableTranslate();
		Vector3* ppos  = ctx.player->GetEditableTranslate();
		if (!pos || !ppos) return;

		// 偏差射撃：着弾までの時間ぶん先の位置を狙う。レールの前進は完全に、
		// プレイヤー自身の移動は shotLeadRate だけ先読みする（動き続ければ避けられ、止まると当たる）。
		float bulletSpeed = kFallbackBulletSpeed;
		if (const PrefabDef* def = PrefabManager::GetInstance()->Find("EnemyBullet"); def && def->hasBullet && def->bulletSpeed > kMinBulletSpeed) {
			bulletSpeed = def->bulletSpeed;
		}
		const Vector3 leadVel{
			ctx.playerRailVelocity.x + ctx.playerOwnVelocity.x * ctx.shotLeadRate,
			ctx.playerRailVelocity.y + ctx.playerOwnVelocity.y * ctx.shotLeadRate,
			ctx.playerRailVelocity.z + ctx.playerOwnVelocity.z * ctx.shotLeadRate };
		// 狙うのは足元（原点）でなく当たり判定の中心
		const Vector3 body = GameScene::GetShotAimPoint(ctx.player);
		Vector3 aim = body;
		for (int i = 0; i < kLeadIterations; ++i) { // 着弾時間→予測位置を2回反復して収束させる
			const float dx = aim.x - pos->x, dy = aim.y - pos->y, dz = aim.z - pos->z;
			const float t = std::sqrt(dx * dx + dy * dy + dz * dz) / bulletSpeed;
			aim = { body.x + leadVel.x * t, body.y + leadVel.y * t, body.z + leadVel.z * t };
		}

		Vector3 dir{ aim.x - pos->x, aim.y - pos->y, aim.z - pos->z };
		const float len = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
		if (len < kNearZeroLength) return;
		dir = { dir.x / len, dir.y / len, dir.z / len };

		// プレイヤーを homingTarget として渡す（ホーミング強度はプレハブから）
		ctx.scene->SpawnEnemyBullet(*pos, dir, -1.0f, -1.0f, "EnemyBullet", ctx.player);
	}
};
