#pragma once
#include "Enemy/Boss/IBossState.h"
#include "Scene/GameScene.h"

/// <summary>
/// 扇状弾：予兆→プレイヤー方向へ扇形に N 発、を角度を半ピッチずつずらして数回連射→硬直。
/// 各斉射の直前に照準を取り直すので、横移動し続けないと隙間に入れない。
/// </summary>
class BossAttackSpread : public BossActionState {
	enum class Phase { Telegraph, Firing, Recover, Done };

public:
	/// <summary>扇状弾を作る</summary>
	/// <param name="bulletCount">1 斉射の弾数</param>
	/// <param name="telegraphTime">予兆の長さ [秒]</param>
	BossAttackSpread(int bulletCount = 7, float telegraphTime = 0.8f)
		: bulletCount_(bulletCount), telegraphTime_(telegraphTime) {}

	/// <summary>予兆から始める（タイマーと斉射数を 0 に）</summary>
	/// <param name="ctx">ボス・自機・シーンと共有データ</param>
	void OnEnter(BossStateContext& ctx) override {
		phase_ = Phase::Telegraph;
		timer_ = 0.0f;
		volleysFired_ = 0;
		ctx.billboardToPlayer = true;
	}

	/// <summary>予兆（黄の点滅）→斉射を間隔を空けて数回→硬直を 1 フレーム進める</summary>
	/// <param name="dt">World の経過時間 [秒]</param>
	/// <param name="ctx">ボス・自機・シーンと共有データ</param>
	void Tick(float dt, BossStateContext& ctx) override {
		ctx.billboardToPlayer = true;
		timer_ += dt;

		if (phase_ == Phase::Telegraph) {
			SetBossTelegraphTint(ctx, { 1.0f, kTelegraphGreen, kTelegraphBlue }, timer_, telegraphTime_); // 弾幕系=黄 / 近接系=白
			if (timer_ >= telegraphTime_) {
				phase_ = Phase::Firing;
				timer_ = volleyInterval_; // 1斉射目を即発射
			}
		} else if (phase_ == Phase::Firing) {
			if (timer_ >= volleyInterval_) {
				timer_ = 0.0f;
				FireVolley(ctx);
				if (++volleysFired_ >= volleyCount_) phase_ = Phase::Recover;
			}
		} else if (phase_ == Phase::Recover) {
			if (timer_ >= recoverTime_) phase_ = Phase::Done;
		}
	}

	/// <summary>扇状弾が終わったか</summary>
	/// <returns>硬直まで終わったら true</returns>
	bool IsFinished() const override { return phase_ == Phase::Done; }

private:
	// 予兆の点滅色（弾幕系＝黄）の緑の成分
	static constexpr float kTelegraphGreen = 0.9f;
	// 予兆の点滅色（弾幕系＝黄）の青の成分
	static constexpr float kTelegraphBlue = 0.2f;

	/// <summary>照準を取り直し、自機の方向へ扇形に 1 斉射する。奇数回目は半ピッチずらす</summary>
	/// <param name="ctx">ボス・自機・シーンと共有データ</param>
	void FireVolley(BossStateContext& ctx) {
		if (!ctx.scene || bulletCount_ <= 0) return;
		const BossAimInfo aim = ComputeBossAim(ctx);
		if (!aim.valid) return;
		const float step = (bulletCount_ > 1) ? spreadAngle_ / static_cast<float>(bulletCount_ - 1) : 0.0f;
		const float start = -spreadAngle_ * 0.5f + ((volleysFired_ % 2 == 1) ? step * 0.5f : 0.0f);
		for (int i = 0; i < bulletCount_; ++i) {
			const Vector3 dir = BossDirFromYawPitch(aim.yaw + start + step * static_cast<float>(i), aim.pitch);
			ctx.scene->SpawnEnemyBullet(aim.origin, dir, -1.0f, -1.0f, "EnemyBullet", nullptr, 0.0f);
		}
	}

	Phase phase_ = Phase::Telegraph;
	float timer_ = 0.0f;
	int   volleysFired_ = 0;

	int   bulletCount_ = 7;
	float telegraphTime_ = 0.8f;
	float spreadAngle_ = 1.047f;   // 扇の全幅 [rad]（60°）
	int   volleyCount_ = 3;
	float volleyInterval_ = 0.35f;
	float recoverTime_ = 0.8f;
};
