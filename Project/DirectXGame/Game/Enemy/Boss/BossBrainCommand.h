#pragma once
#include "Enemy/IEnemyCommand.h"
#include "Enemy/EnemyContext.h"
#include "Enemy/Boss/BossStateMachine.h"
#include "IImGuiEditable.h"
#include "Primitive/PrimitiveInstance.h"

/// <summary>
/// EnemyController（雑魚敵と共通の登録／死亡掃除の配管）に積むための薄いアダプタ。
/// 実際の行動選択・実行は BossStateMachine に丸投げする。雑魚敵の IEnemyCommand 一本道の
/// 実行順序には乗らない＝ボスの行動パターンはこのコマンド1つの中で完結する。
/// </summary>
class BossBrainCommand : public IEnemyCommand {
public:
	BossBrainCommand(const Vector3& arenaCenter = { 0.0f, 0.0f, 0.0f }, float arenaRadius = 0.0f)
		: arenaCenter_(arenaCenter), arenaRadius_(arenaRadius) {}

	void Update(float dt, IImGuiEditable* entity, EnemyContext& ctx) override {
		BossActionContext bctx;
		bctx.boss             = entity;
		bctx.player            = ctx.player;
		bctx.scene             = ctx.scene;
		bctx.billboardToPlayer = ctx.billboardToPlayer;
		bctx.arenaCenter       = arenaCenter_;
		bctx.arenaRadius       = arenaRadius_;

		brain_.Update(dt, bctx);

		ctx.billboardToPlayer = bctx.billboardToPlayer;
		ApplyTint(entity, bctx.tint);
	}

	bool IsFinished() const override { return false; } // 撃破されるまでループ

private:
	// 予兆の点滅色をボス本体（現状は Primitive）へ反映。元の色は初回に控えて、重み 0 で元へ戻す
	void ApplyTint(IImGuiEditable* entity, const Vector4& tint) {
		auto* prim = dynamic_cast<PrimitiveInstance*>(entity);
		if (!prim) return;
		PrimitiveMesh& mesh = prim->GetMesh();
		if (!baseColorCaptured_) {
			baseColor_ = mesh.GetColor();
			baseColorCaptured_ = true;
		}
		const float w = (std::clamp)(tint.w, 0.0f, 1.0f);
		mesh.SetColor({
			baseColor_.x + (tint.x - baseColor_.x) * w,
			baseColor_.y + (tint.y - baseColor_.y) * w,
			baseColor_.z + (tint.z - baseColor_.z) * w,
			baseColor_.w });
	}

	Vector3 arenaCenter_{ 0.0f, 0.0f, 0.0f };
	float   arenaRadius_ = 0.0f;
	Vector4 baseColor_{ 1.0f, 1.0f, 1.0f, 1.0f };
	bool    baseColorCaptured_ = false;
	BossStateMachine brain_;
};
