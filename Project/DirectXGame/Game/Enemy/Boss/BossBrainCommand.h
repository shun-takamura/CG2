#pragma once
#include "Enemy/Commands/IEnemyCommand.h"
#include "Enemy/EnemyContext.h"
#include "Enemy/Boss/BossStateMachine.h"
#include "IImGuiEditable.h"
#include "Primitive/PrimitiveInstance.h"

/// <summary>
/// EnemyController（雑魚敵と共通の登録／死亡掃除の配管）に積むための薄いアダプタ。
/// 実際の行動の選択・実行は BossStateMachine（ステートパターン）に任せる。雑魚敵の IEnemyCommand 一本道の
/// 実行順序には乗らない＝ボスの行動パターンはこのコマンド1つの中で完結する。
/// </summary>
class BossBrainCommand : public IEnemyCommand {
public:
	/// <summary>アリーナと手足を受け取ってボスの頭脳を作る（状態は待機から始まる）</summary>
	/// <param name="arenaCenter">アリーナの中心（ワールド）</param>
	/// <param name="arenaRadius">アリーナの半径 [m]（0 以下なら制限なし）</param>
	/// <param name="rig">ボスの手足（無ければ nullptr）</param>
	BossBrainCommand(const Vector3& arenaCenter = { 0.0f, 0.0f, 0.0f }, float arenaRadius = 0.0f, BossRig* rig = nullptr)
		: arenaCenter_(arenaCenter), arenaRadius_(arenaRadius), rig_(rig) {}

	/// <summary>雑魚敵用のコンテキストをボス用に詰め替えて BossStateMachine を 1 フレーム進め、向きと予兆の色を戻す</summary>
	/// <param name="dt">経過時間 [秒]</param>
	/// <param name="entity">ボス本体の実体</param>
	/// <param name="ctx">雑魚敵と共通のコンテキスト（billboardToPlayer を書き戻す）</param>
	void Update(float dt, IImGuiEditable* entity, EnemyContext& ctx) override {
		BossStateContext bctx;
		bctx.boss             = entity;
		bctx.player            = ctx.player;
		bctx.scene             = ctx.scene;
		bctx.billboardToPlayer = ctx.billboardToPlayer;
		bctx.arenaCenter       = arenaCenter_;
		bctx.arenaRadius       = arenaRadius_;
		bctx.rig               = rig_;

		stateMachine_.Update(dt, bctx);

		ctx.billboardToPlayer = bctx.billboardToPlayer;
		ApplyTint(entity, bctx.tint);
	}

	/// <summary>コマンドが終わったか（撃破されるまでループするので終わらない）</summary>
	/// <returns>常に false</returns>
	bool IsFinished() const override { return false; }

	/// <summary>true の間は次の行動を選ばない（BossStateMachine::SetStandDown）</summary>
	/// <param name="standDown">選ばないなら true</param>
	void SetStandDown(bool standDown) { stateMachine_.SetStandDown(standDown); }
	/// <summary>部位が壊れた：今の行動を打ち切ってひるむ（BossStateMachine::RequestStagger）</summary>
	/// <param name="seconds">ひるむ時間 [秒]</param>
	void RequestStagger(float seconds) { stateMachine_.RequestStagger(seconds); }

private:
	/// <summary>予兆の点滅色をボス本体（現状は Primitive）へ反映する。元の色は初回に控えて、重み 0 で元へ戻す</summary>
	/// <param name="entity">ボス本体（PrimitiveInstance でなければ何もしない）</param>
	/// <param name="tint">重ねる色（rgb）と重み（a、0〜1）</param>
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
	BossRig* rig_ = nullptr;
	Vector4 baseColor_{ 1.0f, 1.0f, 1.0f, 1.0f };
	bool    baseColorCaptured_ = false;
	BossStateMachine stateMachine_;
};
