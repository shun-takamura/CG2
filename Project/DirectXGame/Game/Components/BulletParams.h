#pragma once

/// <summary>
/// 弾プレハブ用パラメータ。HP / DamageDealer と同様に IImGuiEditable に持たせ、
/// Inspector で編集 → "Save as Prefab" でプレハブ JSON に反映される。
/// SpawnEnemyBullet 等のスポーン処理は PrefabDef::bullet から読む。
/// </summary>
struct BulletParams {
	bool  enabled        = false;
	float speed          = 18.0f; // [units/sec]
	float lifetime       = 4.0f;  // [sec]
	float homingStrength = 0.0f;  // [/sec] 0=直進、大きいほど早くターゲット方向に向く
};
