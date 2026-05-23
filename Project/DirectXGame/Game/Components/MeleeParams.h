#pragma once
#include "Vector3.h"

/// <summary>
/// 近接攻撃プレハブ用パラメータ。BulletParams と同様に IImGuiEditable に持たせ、
/// Inspector で編集 → "Save as Prefab" でプレハブ JSON に反映される。
/// SpawnPlayerMelee は PrefabDef::melee* から読む。
///
/// 近接判定は「一定時間持続するヒットボリューム」で、自機に追従する。
/// 同じ敵に多段ヒットしないよう、判定インスタンス側で当てた敵集合を持つ（貫通弾と同じ流儀）。
/// ダメージは「本あて（振り出し）」と「持続あて（遅れ当て）」で倍率を変えられる。
/// </summary>
struct MeleeParams {
	bool    enabled        = false;

	// ----- 攻撃のタイミング（入力 → 発生 → 判定 → 後隙）-----
	float   startup        = 0.05f;            // 入力から判定発生までの時間 [sec]
	float   activeDuration = 0.20f;            // 判定の持続時間 [sec]
	float   recovery       = 0.15f;            // 判定終了から次の行動（射撃/回避/近接）までの後隙 [sec]
	Vector3 offset{ 0.0f, 0.0f, 2.0f };        // aim 基準オフセット：右(X)/上(Y)/前(Z)

	// ----- コンボ進行（弱攻撃の段送り）-----
	float   comboWindow    = 0.40f;            // 後隙終了後、次段入力を受け付ける猶予 [sec]

	// ----- 本あて / 持続あて -----
	float   cleanWindow     = 0.08f;           // 判定発生からこの秒数までを「本あて」扱い
	float   cleanMultiplier = 1.5f;            // 本あて倍率（damage = attackPower × これ）
	float   lateMultiplier  = 0.7f;            // 持続あて倍率
};
