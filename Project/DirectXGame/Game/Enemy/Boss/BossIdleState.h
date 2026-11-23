#pragma once
#include "Enemy/Boss/IBossState.h"

/// <summary>
/// 待機：次の行動を選ぶ状態。行動が終わると必ずここへ戻り、クールタイム＋重み付き抽選で次の状態を返す
/// （一定リズムでの来襲にならないように）。発狂した最初の 1 回は咆哮を挟む。
/// </summary>
class BossIdleState : public IBossState {
public:
	/// <summary>
	/// 次の行動を選んで返す。発狂した最初の 1 回は咆哮、クールタイム明けは確率で攻撃、それ以外は移動系（ジャンプ or 接近）。
	/// 選ばない間（standDown）はこの状態のまま
	/// </summary>
	/// <param name="dt">World の経過時間 [秒]（使わない）</param>
	/// <param name="ctx">ボス・自機・シーンと共有データ</param>
	/// <returns>次の状態（咆哮・攻撃・移動のどれか）。standDown の間は nullptr（待機のまま）</returns>
	std::unique_ptr<IBossState> Update(float dt, BossStateContext& ctx) override;

private:
	/// <summary>攻撃を 1 つ選ぶ（近接レンジ内は近距離技中心、外は弾幕中心の重み付き抽選）。予兆の効果音も鳴らす</summary>
	/// <param name="ctx">ボス・自機・シーンと共有データ</param>
	/// <param name="rage">発狂中か（弾数が増え、予兆が短くなる）</param>
	/// <returns>選んだ攻撃の状態</returns>
	std::unique_ptr<IBossState> SelectAttack(BossStateContext& ctx, bool rage);

	/// <summary>自機が近接レンジ（XZ 距離）の中にいるか</summary>
	/// <param name="ctx">ボス・自機・シーンと共有データ</param>
	/// <returns>中にいれば true</returns>
	static bool IsPlayerInMeleeRange(const BossStateContext& ctx);

	/// <summary>発狂中か（HP 比率が BossBrain::rageHpRatio 以下）</summary>
	/// <param name="ctx">ボス・自機・シーンと共有データ</param>
	/// <returns>発狂中なら true</returns>
	static bool IsRage(const BossStateContext& ctx);
};
