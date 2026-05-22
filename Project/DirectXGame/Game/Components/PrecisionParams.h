#pragma once

/// <summary>
/// プレイヤープレハブ用：精密射撃モード中の弾性能ボーナス（加算値）。
/// 乗算ではなく加算で扱う。発射時に precisionBlend で補間して適用する。
///   speedAdd  … 全弾の弾速に加算
///   homingAdd … ロックオン中の「強ホーミング」強度にのみ加算
/// Inspector で編集 → "Save as Prefab" でプレハブ JSON に反映される。
/// </summary>
struct PrecisionParams {
	bool  enabled   = false;
	float speedAdd  = 0.0f;  // [units/sec] 精密モード中に弾速へ加算（全弾）
	float homingAdd = 0.0f;  // [/sec] 精密モード中に強ホーミング強度へ加算（ロック弾のみ）
};
