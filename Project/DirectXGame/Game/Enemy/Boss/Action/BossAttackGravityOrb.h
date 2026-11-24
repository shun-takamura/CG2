#pragma once
#include "Components/Gameplay.h"
#include "Enemy/Boss/IBossState.h"
#include "Enemy/Boss/Special/BossGravityOrb.h"
#include "IImGuiEditable.h"
#include "Scene/GameScene.h"

/// <summary>
/// ボスの必殺技 GravityOrb：アリーナの中心へ浮き、岩を浮かせて盾の岩だけ残し、溜めた球を落として爆発させる。
/// 段取りと演出はシーンが持つ BossGravityOrb に任せ、このステートはボスの位置を書き込むだけ。
/// 技の間はボスを無敵にし、部位が壊れてもひるまない（演出が途切れないように）。
/// </summary>
class BossAttackGravityOrb : public BossActionState {
public:
	/// <summary>シーンの BossGravityOrb を始め、ボスを無敵にする（シーンに無ければすぐ終わる）</summary>
	/// <param name="ctx">ボス・自機・シーンと共有データ</param>
	void OnEnter(BossStateContext& ctx) override {
		ctx.billboardToPlayer = true;
		finished_ = false;
		orb_ = ctx.scene ? ctx.scene->GetBossGravityOrb() : nullptr;
		const Vector3* bossPosition = ctx.boss ? ctx.boss->GetEditableTranslate() : nullptr;
		if (!orb_ || !bossPosition || !orb_->Begin(*bossPosition, ctx.arenaCenter, ctx.arenaRadius)) {
			orb_ = nullptr;
			finished_ = true;
			return;
		}
		SetInvincible(ctx, true);
	}

	/// <summary>ボスの位置を段取りに合わせ、溜めの間は予兆の色で点滅させる</summary>
	/// <param name="dt">World の経過時間 [秒]</param>
	/// <param name="ctx">ボス・自機・シーンと共有データ</param>
	void Tick(float dt, BossStateContext& ctx) override {
		(void)dt;
		ctx.billboardToPlayer = true;
		if (!orb_) {
			finished_ = true;
			return;
		}
		if (Vector3* bossPosition = ctx.boss ? ctx.boss->GetEditableTranslate() : nullptr) {
			*bossPosition = orb_->GetBossPosition();
		}
		if (orb_->IsCharging()) SetBossTelegraphTint(ctx, kChargeTint, orb_->GetPhaseProgress(), 1.0f);
		if (!orb_->IsRunning()) finished_ = true;
	}

	/// <summary>無敵を戻す。途中で抜けた（ボスが倒れた・リトライ）なら段取りも止める</summary>
	/// <param name="ctx">ボス・自機・シーンと共有データ</param>
	void OnExit(BossStateContext& ctx) override {
		SetInvincible(ctx, false);
		if (orb_ && orb_->IsRunning()) orb_->Abort();
		orb_ = nullptr;
	}

	/// <summary>技の間は部位が壊れてもひるまない</summary>
	/// <returns>常に nullptr（今の状態を続ける）</returns>
	std::unique_ptr<IBossState> OnPartBroken(float staggerSeconds, BossStateContext& ctx) override {
		(void)staggerSeconds;
		(void)ctx;
		return nullptr;
	}

	/// <summary>技が終わったか</summary>
	/// <returns>終わったら true</returns>
	bool IsFinished() const override { return finished_; }

private:
	// 溜めの予兆の点滅の色（青白い光）
	static constexpr Vector3 kChargeTint{ 0.6f, 0.8f, 1.0f };

	static void SetInvincible(BossStateContext& ctx, bool invincible) {
		if (ctx.boss) Gameplay::Of(ctx.boss).GetHP().invincible = invincible;
	}

	BossGravityOrb* orb_ = nullptr;
	bool finished_ = false;
};
