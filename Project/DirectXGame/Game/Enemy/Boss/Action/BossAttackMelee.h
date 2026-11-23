#pragma once
#include <cmath>
#include "Enemy/Boss/IBossState.h"
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
class BossAttackMelee : public BossActionState {
	enum class Phase { Telegraph, Active, Recover, Done };

public:
	/// <summary>近接攻撃を作る</summary>
	/// <param name="telegraphTime">予兆の長さ [秒]</param>
	/// <param name="activeDuration">判定が出ている時間 [秒]（腕で薙ぐ時は薙ぐ時間以上に延ばす）</param>
	/// <param name="recoverTime">硬直の長さ [秒]</param>
	explicit BossAttackMelee(float telegraphTime = 0.6f, float activeDuration = 0.25f, float recoverTime = 0.5f)
		: telegraphTime_(telegraphTime), activeDuration_(activeDuration), recoverTime_(recoverTime) {}

	/// <summary>予兆から始め、薙ぐ腕を選ぶ（両方あれば交互）。手足があるのに腕が両方無ければ何もせず終わる</summary>
	/// <param name="ctx">ボス・自機・シーンと共有データ</param>
	void OnEnter(BossStateContext& ctx) override {
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

	/// <summary>予兆（振りかぶり）→判定（外から内へ薙ぐ）→硬直を 1 フレーム進める</summary>
	/// <param name="dt">World の経過時間 [秒]</param>
	/// <param name="ctx">ボス・自機・シーンと共有データ</param>
	void Tick(float dt, BossStateContext& ctx) override {
		ctx.billboardToPlayer = true;
		timer_ += dt;

		if (phase_ == Phase::Telegraph) {
			SetBossTelegraphTint(ctx, { 1.0f, 1.0f, 1.0f }, timer_, telegraphTime_); // 弾幕系=黄 / 近接系=白
			// 予兆（判定なし）：腕を外へ振りかぶる
			if (useArm_ && ctx.rig) ctx.rig->SetArmTarget(side_, kWindRaise, kWindSwing, kWindSmoothTime);
			if (timer_ >= telegraphTime_) {
				SpawnHitVolume(ctx);
				phase_ = Phase::Active;
				timer_ = 0.0f;
			}
		} else if (phase_ == Phase::Active) {
			// 外から内へ低く薙ぐ。判定は拳に付いていく
			if (useArm_ && ctx.rig) {
				const float t = (activeDuration_ > kMinDuration) ? (std::min)(timer_ / activeDuration_, 1.0f) : 1.0f;
				ctx.rig->SetArmTarget(side_, kSweepRaise, kWindSwing + (kSweepEndSwing - kWindSwing) * t, kSweepSmoothTime);
			}
			UpdateHitVolume(ctx);
			if (timer_ >= activeDuration_) {
				DestroyHitVolume(ctx);
				phase_ = Phase::Recover;
				timer_ = 0.0f;
			}
		} else if (phase_ == Phase::Recover) {
			if (useArm_ && ctx.rig && timer_ < recoverTime_ * kRecoverHoldRate) ctx.rig->SetArmTarget(side_, kSweepRaise, kSweepEndSwing, kRecoverSmoothTime);
			if (timer_ >= recoverTime_) phase_ = Phase::Done;
		}
	}

	/// <summary>途中で打ち切られても判定を残さないよう消す</summary>
	/// <param name="ctx">ボス・自機・シーンと共有データ</param>
	void OnExit(BossStateContext& ctx) override {
		DestroyHitVolume(ctx); // 途中中断でも判定を残さない
	}

	/// <summary>近接攻撃が終わったか</summary>
	/// <returns>硬直まで終わったら true</returns>
	bool IsFinished() const override { return phase_ == Phase::Done; }

private:
	/// <summary>"BossMeleeHit" の判定を拳（無ければ胴の前）に出し、自機の方へ向ける</summary>
	/// <param name="ctx">ボス・自機・シーンと共有データ</param>
	void SpawnHitVolume(BossStateContext& ctx) {
		if (!ctx.scene) return;
		const Vector3 pos = HitPos(ctx);
		hitVolume_ = ctx.scene->SpawnEnemyAt("BossMeleeHit", pos);
		if (hitVolume_) {
			hitVolume_->SetRotate(ComputeFacingRotate(ctx)); // OBB を前方（ボス→プレイヤー方向）へ向ける
		}
	}

	/// <summary>判定を拳（無ければ胴の前）へ付いていかせる</summary>
	/// <param name="ctx">ボス・自機・シーンと共有データ</param>
	void UpdateHitVolume(BossStateContext& ctx) {
		if (!hitVolume_) return;
		if (Vector3* p = hitVolume_->GetEditableTranslate()) {
			*p = HitPos(ctx);
		}
	}

	/// <summary>判定の実体を消す。無ければ何もしない</summary>
	/// <param name="ctx">ボス・自機・シーンと共有データ</param>
	void DestroyHitVolume(BossStateContext& ctx) {
		if (!hitVolume_) return;
		if (ctx.scene) ctx.scene->DestroyDynamicEntity(hitVolume_);
		hitVolume_ = nullptr;
	}

	/// <summary>判定を置く位置を求める</summary>
	/// <param name="ctx">ボス・自機・シーンと共有データ</param>
	/// <returns>腕があれば拳の中心、無ければ従来どおり胴の前（ワールド）</returns>
	Vector3 HitPos(const BossStateContext& ctx) const {
		if (useArm_ && ctx.rig && ctx.rig->IsArmAlive(side_)) return ctx.rig->GetFistPos(side_);
		return ResolveBossHitAnchor(ctx, hitLocalOffset_);
	}

	/// <summary>ボス→自機の水平方向へ向くオイラー角を求める</summary>
	/// <param name="ctx">ボス・自機・シーンと共有データ</param>
	/// <returns>オイラー角 [ラジアン]（Y だけ。自機が無ければ +Z 向き）</returns>
	Vector3 ComputeFacingRotate(const BossStateContext& ctx) const {
		Vector3 fwd{ 0.0f, 0.0f, 1.0f };
		if (ctx.boss && ctx.player) {
			const Vector3* bp = ctx.boss->GetEditableTranslate();
			const Vector3* pp = ctx.player->GetEditableTranslate();
			if (bp && pp) {
				Vector3 d{ pp->x - bp->x, 0.0f, pp->z - bp->z };
				const float len = std::sqrt(d.x * d.x + d.z * d.z);
				if (len > kNearZeroLength) fwd = { d.x / len, 0.0f, d.z / len };
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
	// 振りかぶりの構えへ寄る時間 [秒]
	static constexpr float kWindSmoothTime = 0.12f;
	// 薙ぐ間に構えへ寄る時間 [秒]（素早く追わせる）
	static constexpr float kSweepSmoothTime = 0.03f;
	// 硬直のうち薙ぎ終わりの構えを保つ割合（硬直の長さに対して）
	static constexpr float kRecoverHoldRate = 0.6f;
	// 硬直中に薙ぎ終わりの構えへ寄る時間 [秒]
	static constexpr float kRecoverSmoothTime = 0.08f;
	// これより短い判定の時間は最初から終わりとみなす [秒]
	static constexpr float kMinDuration = 1e-4f;
	// これより短い水平距離では向きを決めない（+Z のまま）[m]
	static constexpr float kNearZeroLength = 1e-4f;
	BossRig::Side side_ = BossRig::Side::Right;
	bool useArm_ = false;
};
