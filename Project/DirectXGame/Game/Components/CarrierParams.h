#pragma once

/// <summary>
/// 運び屋（Carrier）プレハブ用パラメータ。
/// 生成する子敵（雑魚）の寿命と徘徊半径を保持する。
/// Inspector で編集 → "Save as Prefab" でプレハブ JSON に反映される。
/// </summary>
struct CarrierParams {
	bool  enabled            = false;
	float childLifetimeSec   = 10.0f; // 子敵が生成されてから自動退避に入るまでの時間 [秒]
	float childWanderRadius  = 8.0f;  // 子敵が運び屋から離れられる最大半径 [units]
	float childMoveSpeed     = 5.0f;  // 子敵の徘徊移動速度 [units/sec]
};
