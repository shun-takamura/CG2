#pragma once
#include <memory>
#include "Enemy/Boss/IBossState.h"

/// <summary>
/// ボスの状態を持つ本体（ステートパターンのコンテキスト）。今の状態を 1 つだけ持ち、
/// 状態が返した次の状態へ切り替える（どの状態へ行くかは状態クラス自身が決める）。
/// 状態をまたぐ共有データ（クールタイム・乱数・発狂）は BossBrain として持ち、状態へ渡す。
/// </summary>
class BossStateMachine {
public:
	/// <summary>待機（BossIdleState）から始める</summary>
	BossStateMachine();
	/// <summary>破棄（IBossState の派生を完全型で消すため .cpp で定義）</summary>
	~BossStateMachine();

	/// <summary>
	/// 毎フレームの更新。今の状態の Update を呼び、次の状態が返ってきたら切り替える。
	/// 部位破壊の要求があれば、その前に今の状態の OnPartBroken を呼んで移る先を決めさせる
	/// </summary>
	/// <param name="dt">World の経過時間 [秒]</param>
	/// <param name="ctx">ボス・自機・シーン（共有データ brain はここで入れる）</param>
	void Update(float dt, BossStateContext& ctx);

	/// <summary>true の間は次の行動を選ばない（今の行動は最後までやる）。自機の死亡演出の間（19 番 G3）</summary>
	/// <param name="standDown">選ばないなら true</param>
	void SetStandDown(bool standDown) { brain_.standDown = standDown; }

	/// <summary>部位が壊れた：次の Update で今の状態に知らせ、ひるむ</summary>
	/// <param name="seconds">ひるむ時間 [秒]</param>
	void RequestStagger(float seconds) { staggerRequest_ = seconds; }

private:
	/// <summary>状態を切り替える（今の状態の OnExit → 次の状態へ差し替え → 次の状態の OnEnter）</summary>
	/// <param name="next">次の状態（nullptr は渡さない）</param>
	/// <param name="ctx">ボス・自機・シーンと共有データ</param>
	void ChangeState(std::unique_ptr<IBossState> next, BossStateContext& ctx);

	std::unique_ptr<IBossState> current_;
	BossBrain brain_;
	float staggerRequest_ = -1.0f; // >= 0 で次の Update にひるむ
};
