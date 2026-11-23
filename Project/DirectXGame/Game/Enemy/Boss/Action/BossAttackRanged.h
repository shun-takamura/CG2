#pragma once
#include <cmath>
#include "Enemy/Boss/IBossState.h"
#include "Scene/GameScene.h"
#include "IImGuiEditable.h"

/// <summary>
/// 遠距離攻撃：予兆→プレイヤー狙いの弾を1発発射→硬直、で終了する（次の行動は BossStateMachine が選び直す）。
/// 既存 BossAttackCommand（雑魚パイプ側・縦スライス版）の中身を移植したもの。
/// </summary>
class BossAttackRanged : public BossActionState {
	enum class Phase { Telegraph, Active, Recover, Done };

public:
	/// <summary>遠距離攻撃を作る</summary>
	/// <param name="telegraphTime">予兆の長さ [秒]</param>
	/// <param name="recoverTime">硬直の長さ [秒]</param>
	explicit BossAttackRanged(float telegraphTime = 1.2f, float recoverTime = 1.0f)
		: telegraphTime_(telegraphTime), recoverTime_(recoverTime) {}

	/// <summary>予兆から始める（タイマーを 0 に、自機の方を向く）</summary>
	/// <param name="ctx">ボス・自機・シーンと共有データ</param>
	void OnEnter(BossStateContext& ctx) override {
		phase_ = Phase::Telegraph;
		timer_ = 0.0f;
		ctx.billboardToPlayer = true;
	}

	/// <summary>予兆（黄の点滅）→自機狙いの弾を 1 発→硬直を 1 フレーム進める</summary>
	/// <param name="dt">World の経過時間 [秒]</param>
	/// <param name="ctx">ボス・自機・シーンと共有データ</param>
	void Tick(float dt, BossStateContext& ctx) override {
		ctx.billboardToPlayer = true;
		timer_ += dt;

		if (phase_ == Phase::Telegraph) {
			SetBossTelegraphTint(ctx, { 1.0f, kTelegraphGreen, kTelegraphBlue }, timer_, telegraphTime_); // 弾幕系=黄 / 近接系=白
			// 予兆（攻撃なし）。将来ここで telegraph エフェクトを鳴らす。
			if (timer_ >= telegraphTime_) {
				FireAtPlayer(ctx);
				phase_ = Phase::Active;
				timer_ = 0.0f;
			}
		} else if (phase_ == Phase::Active) {
			phase_ = Phase::Recover;
			timer_ = 0.0f;
		} else if (phase_ == Phase::Recover) {
			if (timer_ >= recoverTime_) phase_ = Phase::Done;
		}
	}

	/// <summary>遠距離攻撃が終わったか</summary>
	/// <returns>硬直まで終わったら true</returns>
	bool IsFinished() const override { return phase_ == Phase::Done; }

private:
	// 予兆の点滅色（弾幕系＝黄）の緑の成分
	static constexpr float kTelegraphGreen = 0.9f;
	// 予兆の点滅色（弾幕系＝黄）の青の成分
	static constexpr float kTelegraphBlue = 0.2f;
	// これより自機に近いときは撃たない（向きが決まらない）[m]
	static constexpr float kNearZeroLength = 1e-4f;

	/// <summary>ボスの位置から自機へ向けて追尾つきの弾を 1 発撃つ</summary>
	/// <param name="ctx">ボス・自機・シーンと共有データ</param>
	void FireAtPlayer(BossStateContext& ctx) {
		if (!ctx.boss || !ctx.player || !ctx.scene) return;
		const Vector3* bp = ctx.boss->GetEditableTranslate();
		const Vector3* pp = ctx.player->GetEditableTranslate();
		if (!bp || !pp) return;
		Vector3 d{ pp->x - bp->x, pp->y - bp->y, pp->z - bp->z };
		const float len = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
		if (len < kNearZeroLength) return;
		d = { d.x / len, d.y / len, d.z / len };
		// 弾速/寿命/ホーミングは EnemyBullet プレハブの bullet セクションから（負数＝プレハブ既定）。
		ctx.scene->SpawnEnemyBullet(*bp, d, -1.0f, -1.0f, "EnemyBullet", ctx.player);
	}

	Phase phase_         = Phase::Telegraph;
	float timer_         = 0.0f;
	float telegraphTime_ = 1.2f;
	float recoverTime_   = 1.0f;
};
