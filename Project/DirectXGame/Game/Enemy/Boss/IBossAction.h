#pragma once

class IImGuiEditable;
class GameScene;

/// <summary>
/// ボス行動（移動/攻撃/反撃）に渡すコンテキスト。雑魚敵の EnemyContext とは独立して定義し、
/// ボス側の変更が雑魚敵コードに波及しないようにする。
/// </summary>
struct BossActionContext {
	IImGuiEditable* boss   = nullptr;
	IImGuiEditable* player = nullptr;
	GameScene*      scene  = nullptr;
	bool billboardToPlayer = true; // out: プレイヤー方向を向くか（ビルボード制御）
};

/// <summary>
/// ボス行動の共通インターフェース。BossActionManager が1つだけ保持して駆動する。
/// </summary>
struct IBossAction {
	virtual void OnEnter(BossActionContext& ctx) {}
	virtual void Update(float dt, BossActionContext& ctx) = 0;
	virtual void OnExit(BossActionContext& ctx) {}
	virtual bool IsFinished() const = 0;
	virtual ~IBossAction() = default;
};
