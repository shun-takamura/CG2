#pragma once

class IImGuiEditable;
struct EnemyContext;

/// <summary>
/// 敵 AI コマンドの基底インターフェース。
/// 各コマンドは EnemyController によって順番に実行される。
/// </summary>
struct IEnemyCommand {
	virtual void OnEnter(IImGuiEditable* entity, EnemyContext& ctx) {}
	virtual void Update(float dt, IImGuiEditable* entity, EnemyContext& ctx) = 0;
	virtual void OnExit(IImGuiEditable* entity, EnemyContext& ctx) {}
	virtual bool IsFinished() const = 0;
	virtual ~IEnemyCommand() = default;
};
