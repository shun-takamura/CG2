#pragma once
#include <string>

/// <summary>
/// 弾プレハブ用パラメータ。HP / DamageDealer と同様に IImGuiEditable に持たせ、
/// Inspector で編集 → "Save as Prefab" でプレハブ JSON に反映される。
/// SpawnEnemyBullet 等のスポーン処理は PrefabDef::bullet から読む。
/// </summary>
struct BulletParams {
	bool  enabled        = false;
	float speed          = 18.0f; // [units/sec]
	float lifetime       = 4.0f;  // [sec]
	float homingStrength = 0.0f;  // [/sec] 軽ホーミング：最近敵へ向かう。0=直進
	float strongHomingStrength = 0.0f; // [/sec] 強ホーミング：レティクルが重なってロック中の敵へ向かう
	float colliderGrowth = 0.02f; // 進行 1m あたりの collider 半径拡大量

	// ----- 貫通 -----
	bool        penetrate           = false; // true で敵を貫通（消えずに進む）
	float       penetrateDamageRate = 0.2f;  // 貫通中、同じ敵に多段ヒットする間隔 [sec]
	std::string penetrateEffect;             // 貫通中ダメージ発生時に再生するエフェクト名
};
