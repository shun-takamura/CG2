#include "Enemy/Boss/BossIdleState.h"
#include "Enemy/Boss/Action/BossAttackMelee.h"
#include "Enemy/Boss/Action/BossAttackRanged.h"
#include "Enemy/Boss/Action/BossMoveApproach.h"
#include "Enemy/Boss/Action/BossMoveJumpOver.h"
#include "Enemy/Boss/Action/BossAttackSpread.h"
#include "Enemy/Boss/Action/BossAttackRadialBurst.h"
#include "Enemy/Boss/Action/BossAttackCharge.h"
#include "Enemy/Boss/Action/BossAttackSlam.h"
#include "Enemy/Boss/Action/BossRageRoar.h"
#include "Enemy/Boss/BossRig.h"
#include "Components/Gameplay.h"
#include "IImGuiEditable.h"
#include "SoundManager.h"
#include "Score/DifficultyProfile.h"
#include "Score/GameSession.h"
#include "Scene/GameScene.h"

namespace {
	// 攻撃の種類（重み付き抽選の添字）
	enum AttackKind { kMelee, kSlam, kRadial, kCharge, kSpread, kRanged, kAttackKindCount };
	// 重み。近接レンジ内は近距離技中心、レンジ外は弾幕中心
	constexpr float kNearWeights[kAttackKindCount] = { 35.0f, 20.0f, 25.0f, 20.0f, 0.0f, 0.0f };
	constexpr float kFarWeights[kAttackKindCount]  = { 0.0f, 15.0f, 20.0f, 15.0f, 30.0f, 20.0f };

	// 弾数（通常 / 発狂）
	constexpr int kSlamShockwaveBullets[2]  = { 24, 32 };
	constexpr int kRadialBurstBullets[2]    = { 20, 28 };
	constexpr int kSpreadVolleyBullets[2]   = { 7, 9 };
	// 予兆の長さ [秒]（発狂中は BossBrain::rageTelegraphScale を掛けて短くする）
	constexpr float kMeleeTelegraph  = 0.6f;
	constexpr float kSlamTelegraph   = 0.5f;
	constexpr float kRadialTelegraph = 1.0f;
	constexpr float kChargeTelegraph = 0.9f;
	constexpr float kSpreadTelegraph = 0.8f;
	constexpr float kRangedTelegraph = 1.2f;

	// 咆哮の直後はすぐ攻撃に移れるよう、クールタイムを短くする [秒]
	constexpr float kAfterRoarAttackCooldown = 0.3f;
}

std::unique_ptr<IBossState> BossIdleState::Update(float dt, BossStateContext& ctx) {
	(void)dt;
	BossBrain& brain = *ctx.brain;
	if (brain.standDown) return nullptr;

	std::uniform_real_distribution<float> dist(0.0f, 1.0f);
	const bool rage = IsRage(ctx);

	// 発狂した最初の 1 回は咆哮
	if (rage && !brain.rageEntered) {
		brain.rageEntered = true;
		brain.attackCooldown = kAfterRoarAttackCooldown;
		return std::make_unique<BossRageRoar>();
	}
	// クールタイムが明けていれば確率で攻撃
	if (brain.attackCooldown <= 0.0f && dist(brain.rng) < (rage ? brain.rageAttackChance : brain.attackChance)) {
		brain.attackCooldown = (rage ? brain.rageAttackCooldown : brain.attackCooldownBase)
			* ctx.scene->GetGameSession()->GetDifficultyProfile()->GetBossCooldownScale();
		return SelectAttack(ctx, rage);
	}
	// 攻撃しないときは移動（ジャンプで飛び越える or 近づく）
	if (brain.jumpCooldown <= 0.0f && dist(brain.rng) < brain.jumpChance) {
		brain.jumpCooldown = brain.jumpCooldownBase;
		return std::make_unique<BossMoveJumpOver>();
	}
	return std::make_unique<BossMoveApproach>();
}

std::unique_ptr<IBossState> BossIdleState::SelectAttack(BossStateContext& ctx, bool rage) {
	BossBrain& brain = *ctx.brain;
	float weights[kAttackKindCount];
	const float* base = IsPlayerInMeleeRange(ctx) ? kNearWeights : kFarWeights;
	for (int i = 0; i < kAttackKindCount; ++i) weights[i] = base[i];
	// 腕が両方壊れたら腕の薙ぎ払いは無い（その分は突進へ）
	if (ctx.rig && !ctx.rig->HasAnyArm()) {
		weights[kCharge] += weights[kMelee];
		weights[kMelee] = 0.0f;
	}

	float total = 0.0f;
	for (int i = 0; i < kAttackKindCount; ++i) total += weights[i];
	std::uniform_real_distribution<float> dist(0.0f, total);
	float r = dist(brain.rng);
	int kind = kRanged;
	for (int i = 0; i < kAttackKindCount; ++i) {
		if (r < weights[i]) { kind = i; break; }
		r -= weights[i];
	}

	const float tele = rage ? brain.rageTelegraphScale : 1.0f;
	const int level = rage ? 1 : 0;
	SoundManager::GetInstance()->Play2DSound("se_boss_telegraph");
	switch (kind) {
	case kMelee:  return std::make_unique<BossAttackMelee>(kMeleeTelegraph * tele);
	case kSlam:   return std::make_unique<BossAttackSlam>(kSlamShockwaveBullets[level], kSlamTelegraph * tele);
	case kRadial: return std::make_unique<BossAttackRadialBurst>(kRadialBurstBullets[level], kRadialTelegraph * tele);
	case kCharge: return std::make_unique<BossAttackCharge>(kChargeTelegraph * tele);
	case kSpread: return std::make_unique<BossAttackSpread>(kSpreadVolleyBullets[level], kSpreadTelegraph * tele);
	default:      return std::make_unique<BossAttackRanged>(kRangedTelegraph * tele);
	}
}

bool BossIdleState::IsRage(const BossStateContext& ctx) {
	if (!ctx.boss) return false;
	const HP& hp = Gameplay::Of(ctx.boss).GetHP();
	if (!hp.enabled || hp.maxHP <= 0) return false;
	return static_cast<float>(hp.currentHP) <= static_cast<float>(hp.maxHP) * ctx.brain->rageHpRatio;
}

bool BossIdleState::IsPlayerInMeleeRange(const BossStateContext& ctx) {
	if (!ctx.boss || !ctx.player) return false;
	const Vector3* bp = ctx.boss->GetEditableTranslate();
	const Vector3* pp = ctx.player->GetEditableTranslate();
	if (!bp || !pp) return false;
	const float dx = pp->x - bp->x;
	const float dz = pp->z - bp->z;
	const float range = ctx.brain->meleeRange;
	return (dx * dx + dz * dz) <= range * range;
}
