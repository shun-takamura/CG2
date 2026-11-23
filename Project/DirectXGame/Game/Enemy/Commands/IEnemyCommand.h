#pragma once

class IImGuiEditable;
struct EnemyContext;

/// <summary>
/// 敵 AI コマンドの基底インターフェース。
/// 各コマンドは EnemyController によって順番に実行される。
/// </summary>
struct IEnemyCommand {
	/// <summary>このコマンドが実行順に来て最初の Update の直前に 1 回だけ呼ばれる。既定は何もしない</summary>
	/// <param name="entity">動かす敵</param>
	/// <param name="ctx">敵の入力（自機・時刻など）と結果の書き込み先</param>
	virtual void OnEnter(IImGuiEditable* entity, EnemyContext& ctx) {}
	/// <summary>毎フレーム 1 回呼ばれる、このコマンドの更新。移動・射撃などの結果は ctx の out 系へ書く</summary>
	/// <param name="dt">経過時間 [秒]</param>
	/// <param name="entity">動かす敵</param>
	/// <param name="ctx">敵の入力（自機・時刻など）と結果の書き込み先</param>
	virtual void Update(float dt, IImGuiEditable* entity, EnemyContext& ctx) = 0;
	/// <summary>IsFinished が true になって次のコマンドへ進む時に 1 回だけ呼ばれる。既定は何もしない</summary>
	/// <param name="entity">動かす敵</param>
	/// <param name="ctx">敵の入力（自機・時刻など）と結果の書き込み先</param>
	virtual void OnExit(IImGuiEditable* entity, EnemyContext& ctx) {}
	/// <summary>このコマンドが終わったか。Update の直後に EnemyController が見る</summary>
	/// <returns>終わったら true（次のコマンドへ進む）。続けるなら false</returns>
	virtual bool IsFinished() const = 0;
	virtual ~IEnemyCommand() = default;
};
