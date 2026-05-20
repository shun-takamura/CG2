#pragma once

/// <summary>
/// ダメージを与える側のコンポーネント。攻撃エンティティ（弾・突進判定・ボスの攻撃判定）に付ける。
/// 敵側は damage を固定値として JSON で設定する。
/// プレイヤー攻撃側はプレハブで multiplier だけ持ち、発射時に
///   damage = static_cast<int>(playerAttackPower * multiplier)
/// として動的にセットする。
/// </summary>
struct DamageDealer {
	bool  enabled = false;
	int   damage = 0;        // 最終ダメージ値
	float multiplier = 1.0f; // プレイヤー攻撃倍率（敵は 1.0 固定でも可）
};
