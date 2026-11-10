#pragma once
#include <cmath>
#include "Enemy/Boss/IBossAction.h"
#include "Scene/GameScene.h"
#include "IImGuiEditable.h"
#include "Enemy/Boss/BossRig.h"

/// <summary>
/// 近接攻撃：予兆→ヒット判定（一定時間持続）→硬直。
/// アニメーション未実装のため、判定は "BossMeleeHit" プレハブ（半透明プリミティブ＋Collider）を
/// ResolveBossHitAnchor（ボス root 基準）の位置に生成し、Collider のワイヤーフレーム表示
/// （CollisionManager::DrawDebug、_DEBUG時のみ）で代用する。
/// アニメ導入後は ResolveBossHitAnchor の中身をボーン基準に差し替えるだけでよく、
/// このクラス自体（タイミング・生成/追従/破棄のロジック）は変更不要。
/// </summary>
class BossAttackMelee : public IBossAction {
	enum class Phase { Telegraph, Active, Recover, Done };

public:
	explicit BossAttackMelee(float telegraphTime = 0.6f, float activeDuration = 0.25f, float recoverTime = 0.5f)
		: telegraphTime_(telegraphTime), activeDuration_(activeDuration), recoverTime_(recoverTime) {}

	void OnEnter(BossActionContext& ctx) override {
		phase_ = Phase::Telegraph;
		timer_ = 0.0f;
		ctx.billboardToPlayer = true;
		if (ctx.rig) {
			// 残っている腕で薙ぐ（両方あれば交互）。腕が無ければ何もしない
			static bool sUseRight = true;
			sUseRight = !sUseRight;
			const BossRig::Side first = sUseRight ? BossRig::Side::Right : BossRig::Side::Left;
			const BossRig::Side second = sUseRight ? BossRig::Side::Left : BossRig::Side::Right;
			if (ctx.rig->IsArmAlive(first)) side_ = first;
			else if (ctx.rig->IsArmAlive(second)) side_ = second;
			else { phase_ = Phase::Done; return; }
			useArm_ = true;
			activeDuration_ = (std::max)(activeDuration_, kArmSweepTime);
		}
	}

	void Update(float dt, BossActionContext& ctx) override {
		ctx.billboardToPlayer = true;
		timer_ += dt;

		if (phase_ == Phase::Telegraph) {
			SetBossTelegraphTint(ctx, { 1.0f, 1.0f, 1.0f }, timer_, telegraphTime_); // 弾幕系=黄 / 近接系=白
			// 予兆（判定なし）：腕を外へ振りかぶる
			if (useArm_ && ctx.rig) ctx.rig->SetArmTarget(side_, kWindRaise, kWindSwing, 0.12f);
			if (timer_ >= telegraphTime_) {
				SpawnHitVolume(ctx);
				phase_ = Phase::Active;
				timer_ = 0.0f;
			}
		} else if (phase_ == Phase::Active) {
			// 外から内へ低く薙ぐ。判定は拳に付いていく
			if (useArm_ && ctx.rig) {
				const float t = (activeDuration_ > 1e-4f) ? (std::min)(timer_ / activeDuration_, 1.0f) : 1.0f;
				ctx.rig->SetArmTarget(side_, kSweepRaise, kWindSwing + (kSweepEndSwing - kWindSwing) * t, 0.03f);
			}
			UpdateHitVolume(ctx);
			if (timer_ >= activeDuration_) {
				DestroyHitVolume(ctx);
				phase_ = Phase::Recover;
				timer_ = 0.0f;
			}
		} else if (phase_ == Phase::Recover) {
			if (useArm_ && ctx.rig && timer_ < recoverTime_ * 0.6f) ctx.rig->SetArmTarget(side_, kSweepRaise, kSweepEndSwing, 0.08f);
			if (timer_ >= recoverTime_) phase_ = Phase::Done;
		}
	}

	void OnExit(BossActionContext& ctx) override {
		DestroyHitVolume(ctx); // 途中中断でも判定を残さない
	}

	bool IsFinished() const override { return phase_ == Phase::Done; }

private:
	void SpawnHitVolume(BossActionContext& ctx) {
		if (!ctx.scene) return;
		const Vector3 pos = HitPos(ctx);
		hitVolume_ = ctx.scene->SpawnEnemyAt("BossMeleeHit", pos);
		if (hitVolume_) {
			hitVolume_->SetRotate(ComputeFacingRotate(ctx)); // OBB を前方（ボス→プレイヤー方向）へ向ける
		}
	}

	void UpdateHitVolume(BossActionContext& ctx) {
		if (!hitVolume_) return;
		if (Vector3* p = hitVolume_->GetEditableTranslate()) {
			*p = HitPos(ctx);
		}
	}

	void DestroyHitVolume(BossActionContext& ctx) {
		if (!hitVolume_) return;
		if (ctx.scene) ctx.scene->DestroyDynamicEntity(hitVolume_);
		hitVolume_ = nullptr;
	}

	// 腕があれば拳、無ければ従来どおり胴の前
	Vector3 HitPos(const BossActionContext& ctx) const {
		if (useArm_ && ctx.rig && ctx.rig->IsArmAlive(side_)) return ctx.rig->GetFistPos(side_);
		return ResolveBossHitAnchor(ctx, hitLocalOffset_);
	}

	Vector3 ComputeFacingRotate(const BossActionContext& ctx) const {
		Vector3 fwd{ 0.0f, 0.0f, 1.0f };
		if (ctx.boss && ctx.player) {
			const Vector3* bp = ctx.boss->GetEditableTranslate();
			const Vector3* pp = ctx.player->GetEditableTranslate();
			if (bp && pp) {
				Vector3 d{ pp->x - bp->x, 0.0f, pp->z - bp->z };
				const float len = std::sqrt(d.x * d.x + d.z * d.z);
				if (len > 1e-4f) fwd = { d.x / len, 0.0f, d.z / len };
			}
		}
		return { 0.0f, std::atan2(fwd.x, fwd.z), 0.0f };
	}

	Phase phase_          = Phase::Telegraph;
	float timer_          = 0.0f;
	float telegraphTime_  = 0.6f;
	float activeDuration_ = 0.25f;
	float recoverTime_    = 0.5f;
	Vector3 hitLocalOffset_{ 0.0f, 0.0f, 4.0f }; // アンカーからの前方オフセット（間合い）

	IImGuiEditable* hitVolume_ = nullptr;

	// 腕の薙ぎ払い（BossRig の raise / swing）
	static constexpr float kArmSweepTime  = 0.35f; // 薙ぐ時間（判定の出ている時間）
	static constexpr float kWindRaise     = 1.9f;  // 振りかぶり：肩より上
	static constexpr float kWindSwing     = -1.0f; // 振りかぶり：外へ
	static constexpr float kSweepRaise    = 0.8f;  // 薙ぐ高さ：拳が自機の胸のあたり
	static constexpr float kSweepEndSwing = 1.3f;  // 薙ぎ終わり：体の内側の先まで
	BossRig::Side side_ = BossRig::Side::Right;
	bool useArm_ = false;
};
