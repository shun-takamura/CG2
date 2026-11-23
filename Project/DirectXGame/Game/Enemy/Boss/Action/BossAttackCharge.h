#pragma once
#include "Enemy/Boss/IBossState.h"
#include "Scene/GameScene.h"
#include "Enemy/Boss/BossRig.h"

/// <summary>
/// 突進：予兆（プレイヤーを向いて溜め）→予兆終了時のプレイヤー位置を突き抜ける直線ダッシュ→硬直。
/// ダッシュ中は "BossMeleeHit"（EnemyAttack の判定プレハブ）を体の前に追従させて接触ダメージにする。
/// 目標は予兆の終わりに一度だけ確定するので、溜めを見てから横に避ければかわせる。
/// </summary>
class BossAttackCharge : public BossActionState {
	enum class Phase { Telegraph, Dash, Recover, Done };

public:
	/// <summary>突進を作る</summary>
	/// <param name="telegraphTime">予兆（溜め）の長さ [秒]</param>
	explicit BossAttackCharge(float telegraphTime = 0.9f)
		: telegraphTime_(telegraphTime) {}

	/// <summary>予兆から始める（タイマーを 0 に、自機の方を向く）</summary>
	/// <param name="ctx">ボス・自機・シーンと共有データ</param>
	void OnEnter(BossStateContext& ctx) override {
		phase_ = Phase::Telegraph;
		timer_ = 0.0f;
		ctx.billboardToPlayer = true;
	}

	/// <summary>予兆（溜め・点滅）→ダッシュ（判定を体の前に追従）→硬直を 1 フレーム進める</summary>
	/// <param name="dt">World の経過時間 [秒]</param>
	/// <param name="ctx">ボス・自機・シーンと共有データ</param>
	void Tick(float dt, BossStateContext& ctx) override {
		timer_ += dt;

		if (phase_ == Phase::Telegraph) {
			SetBossTelegraphTint(ctx, { 1.0f, 1.0f, 1.0f }, timer_, telegraphTime_); // 弾幕系=黄 / 近接系=白
			ctx.billboardToPlayer = true;
			SetArms(ctx, kWindRaise, kWindSwing, kWindSmoothTime); // 両腕を後ろへ引いて溜める
			if (timer_ >= telegraphTime_) {
				if (!BeginDash(ctx)) { phase_ = Phase::Done; return; }
				phase_ = Phase::Dash;
				timer_ = 0.0f;
			}
		} else if (phase_ == Phase::Dash) {
			// 突進中は向きを固定（振り向かない）。両腕を前へ突き出す
			ctx.billboardToPlayer = false;
			SetArms(ctx, kDashRaise, kDashSwing, kDashSmoothTime);
			float t = (dashDuration_ > kMinDuration) ? (timer_ / dashDuration_) : 1.0f;
			if (t > 1.0f) t = 1.0f;
			const float e = t * t * (3.0f - 2.0f * t);
			if (Vector3* bp = ctx.boss ? ctx.boss->GetEditableTranslate() : nullptr) {
				bp->x = startPos_.x + (goalPos_.x - startPos_.x) * e;
				bp->z = startPos_.z + (goalPos_.z - startPos_.z) * e;
			}
			if (hitVolume_) {
				if (Vector3* hp = hitVolume_->GetEditableTranslate()) {
					*hp = ResolveBossHitAnchor(ctx, hitLocalOffset_);
				}
			}
			if (t >= 1.0f) {
				DestroyHitVolume(ctx);
				phase_ = Phase::Recover;
				timer_ = 0.0f;
			}
		} else if (phase_ == Phase::Recover) {
			ctx.billboardToPlayer = true;
			if (timer_ >= recoverTime_) phase_ = Phase::Done;
		}
	}

	/// <summary>途中で打ち切られても接触判定を残さないよう消す</summary>
	/// <param name="ctx">ボス・自機・シーンと共有データ</param>
	void OnExit(BossStateContext& ctx) override {
		DestroyHitVolume(ctx);
	}

	/// <summary>突進が終わったか</summary>
	/// <returns>硬直まで終わったら true（ダッシュを始められなかった時も true）</returns>
	bool IsFinished() const override { return phase_ == Phase::Done; }

private:
	/// <summary>ダッシュの始点・終点（自機を突き抜けた先、アリーナ内）を確定し、接触判定を出す</summary>
	/// <param name="ctx">ボス・自機・シーンと共有データ</param>
	/// <returns>始められたら true。照準が取れない・シーンが無ければ false</returns>
	bool BeginDash(BossStateContext& ctx) {
		const BossAimInfo aim = ComputeBossAim(ctx);
		if (!aim.valid || !ctx.scene) return false;
		startPos_ = aim.origin;
		// プレイヤーの位置を overshoot_ だけ突き抜ける
		const float len = aim.distXZ + overshoot_;
		goalPos_ = {
			startPos_.x + std::sin(aim.yaw) * len,
			startPos_.y,
			startPos_.z + std::cos(aim.yaw) * len,
		};
		ClampToBossArena(ctx, goalPos_);
		hitVolume_ = ctx.scene->SpawnEnemyAt("BossMeleeHit", ResolveBossHitAnchor(ctx, hitLocalOffset_));
		if (hitVolume_) hitVolume_->SetRotate({ 0.0f, aim.yaw, 0.0f });
		return true;
	}

	/// <summary>両腕に同じ構えを指示する。手足が無ければ何もしない</summary>
	/// <param name="ctx">ボス・自機・シーンと共有データ</param>
	/// <param name="raise">構え [ラジアン]</param>
	/// <param name="swing">振り [ラジアン]</param>
	/// <param name="smoothTime">目標の構えへ寄る時間 [秒]</param>
	static void SetArms(BossStateContext& ctx, float raise, float swing, float smoothTime) {
		if (!ctx.rig) return;
		ctx.rig->SetArmTarget(BossRig::Side::Left, raise, swing, smoothTime);
		ctx.rig->SetArmTarget(BossRig::Side::Right, raise, swing, smoothTime);
	}

	/// <summary>接触判定の実体を消す。無ければ何もしない</summary>
	/// <param name="ctx">ボス・自機・シーンと共有データ</param>
	void DestroyHitVolume(BossStateContext& ctx) {
		if (!hitVolume_) return;
		if (ctx.scene) ctx.scene->DestroyDynamicEntity(hitVolume_);
		hitVolume_ = nullptr;
	}

	// 溜め：両腕を後ろへ引く構え [rad]
	static constexpr float kWindRaise = -0.5f;
	// 溜め：両腕の振り [rad]
	static constexpr float kWindSwing = -0.4f;
	// 溜め：構えへ寄る時間 [秒]
	static constexpr float kWindSmoothTime = 0.2f;
	// 突進：両腕を前へ突き出す構え [rad]
	static constexpr float kDashRaise = 1.45f;
	// 突進：両腕の振り [rad]
	static constexpr float kDashSwing = 0.25f;
	// 突進：構えへ寄る時間 [秒]
	static constexpr float kDashSmoothTime = 0.05f;
	// これより短い突進の時間は最初から終わりとみなす [秒]
	static constexpr float kMinDuration = 1e-4f;

	Phase phase_ = Phase::Telegraph;
	float timer_ = 0.0f;

	float telegraphTime_ = 0.9f;
	float dashDuration_ = 0.6f;
	float overshoot_ = 6.0f;          // プレイヤー位置からさらに進む距離 [m]
	float recoverTime_ = 0.8f;
	// 胴の中心は脚の分だけ高い（地面から 3.4m）ので、判定は自機の胸の高さまで下げる
	Vector3 hitLocalOffset_{ 0.0f, -1.6f, 1.5f };

	Vector3 startPos_{ 0.0f, 0.0f, 0.0f };
	Vector3 goalPos_{ 0.0f, 0.0f, 0.0f };
	IImGuiEditable* hitVolume_ = nullptr;
};
