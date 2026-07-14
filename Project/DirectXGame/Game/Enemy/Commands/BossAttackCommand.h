#pragma once
#include <cmath>
#include "Enemy/IEnemyCommand.h"
#include "Enemy/EnemyContext.h"
#include "Scene/GameScene.h"
#include "IImGuiEditable.h"

/// <summary>
/// ボス（縦スライス最小構成）の攻撃コマンド。ループして終了しない。
///   Phase::Telegraph : telegraphTime_ 秒、プレイヤー方向を向いて予兆（攻撃なし）
///   Phase::Active    : プレイヤー狙いの弾を1発発射（EnemyAttack タグ＝ジャスト回避対象）
///   Phase::Recover   : recoverTime_ 秒の硬直後、Telegraph へ戻る
/// タイマーはコマンド内部で持ち、stageTimeSec には依存しない（静止ボスのため）。
/// </summary>
class BossAttackCommand : public IEnemyCommand {
	enum class Phase { Telegraph, Active, Recover };

public:
	explicit BossAttackCommand(float telegraphTime = 1.2f, float recoverTime = 1.0f)
		: telegraphTime_(telegraphTime), recoverTime_(recoverTime) {}

	void OnEnter(IImGuiEditable* /*entity*/, EnemyContext& ctx) override {
		phase_ = Phase::Telegraph;
		timer_ = 0.0f;
		ctx.billboardToPlayer = true;
	}

	void Update(float dt, IImGuiEditable* entity, EnemyContext& ctx) override {
		ctx.billboardToPlayer = true; // 常にプレイヤーを向く（静止ボス）
		timer_ += dt;

		if (phase_ == Phase::Telegraph) {
			// 予兆（攻撃なし）。将来ここで telegraph エフェクトを鳴らす。
			if (timer_ >= telegraphTime_) {
				FireAtPlayer(entity, ctx);
				phase_ = Phase::Active;
				timer_ = 0.0f;
			}
		} else if (phase_ == Phase::Active) {
			// 発射直後。1フレームで硬直へ（連射にしたい場合はここを拡張）。
			phase_ = Phase::Recover;
			timer_ = 0.0f;
		} else { // Recover
			if (timer_ >= recoverTime_) {
				phase_ = Phase::Telegraph;
				timer_ = 0.0f;
			}
		}
	}

	bool IsFinished() const override { return false; } // 撃破されるまでループ

private:
	void FireAtPlayer(IImGuiEditable* entity, EnemyContext& ctx) {
		if (!entity || !ctx.player || !ctx.scene) return;
		const Vector3* bp = entity->GetEditableTranslate();
		const Vector3* pp = ctx.player->GetEditableTranslate();
		if (!bp || !pp) return;
		Vector3 d{ pp->x - bp->x, pp->y - bp->y, pp->z - bp->z };
		const float len = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
		if (len < 1e-4f) return;
		d = { d.x / len, d.y / len, d.z / len };
		// 弾速/寿命/ホーミングは EnemyBullet プレハブの bullet セクションから（負数＝プレハブ既定）。
		ctx.scene->SpawnEnemyBullet(*bp, d, -1.0f, -1.0f, "EnemyBullet", ctx.player);
	}

	Phase phase_         = Phase::Telegraph;
	float timer_         = 0.0f;
	float telegraphTime_ = 1.2f;
	float recoverTime_   = 1.0f;
};
