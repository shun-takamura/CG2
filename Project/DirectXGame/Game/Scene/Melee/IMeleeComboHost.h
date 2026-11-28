#pragma once
#include <string>

/// <summary>
/// 近接コンボ（MeleeComboPart）がシーンへ頼むことだけを並べた窓口（IPlayerBeamHost と同じ流儀）。
/// </summary>
class IMeleeComboHost {
public:
	virtual ~IMeleeComboHost() = default;
	/// <summary>近接の判定を出す（足元から持ち上げ・ボス戦は小さく・音を鳴らす はシーン側）</summary>
	/// <param name="prefab">判定のプレハブ名</param>
	/// <param name="damageScale">攻撃力に掛ける倍率</param>
	/// <param name="rehitInterval">同じ相手にもう一度当たれるまでの実時間 [秒]（0 以下なら 1 回だけ。ヒットストップでも止まらない）</param>
	virtual void SpawnComboMelee(const std::string& prefab, float damageScale, float rehitInterval) = 0;
	/// <summary>自機のクリップを再生する</summary>
	/// <param name="clip">.anim のパス（空は来ない）</param>
	/// <param name="speed">再生速度</param>
	/// <param name="fade">フェードの時間 [秒]</param>
	/// <param name="loop">繰り返すか</param>
	virtual void PlayComboClip(const std::string& clip, float speed, float fade, bool loop) = 0;
	/// <summary>攻撃・溜めが終わったので、自機の姿勢をいつもの姿勢へ戻す</summary>
	virtual void RestoreComboPose() = 0;
	/// <summary>攻撃を出した（射撃の「攻撃中」判定・移動状態の切り替え用）</summary>
	virtual void OnComboAttackStarted() = 0;
	/// <summary>銃のチャージを使い切る（RT+Y+B の大技）</summary>
	/// <returns>使う前のチャージの段階（0＝無し, 1, 2）</returns>
	virtual int ConsumeGunCharge() = 0;
};
