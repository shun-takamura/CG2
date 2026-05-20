#pragma once

#include <string>
#include <vector>
#include "Vector3.h"

/// <summary>
/// ステージの1スポーンエントリ。
/// カメラ進行度 t（正規化 0〜1）をトリガーに敵をスポーン/退避させる。
/// </summary>
struct WaveEntry {
	std::string enemyType;        // "Drone" / "Carrier" / "Rusher" など
	std::string prefab;           // PrefabManager に登録された名前
	float       triggerT  = 0.0f; // スポーントリガー（カメラ進行度 t）
	float       retreatT  = -1.0f;// 退避トリガー（-1 = なし、突進型など消滅で終わるタイプ）
	float       traverseT      = 0.15f; // 敵スプラインを踏破するのにかかるカメラ t 量。
	                                    // 例: 0.15 で「カメラ t が 0.15 進む間」（120秒スケールなら18秒）かけて踏破。
	std::string splineId;               // 移動スプライン名（Drone の固定ルート / Rusher の登場スプライン）
	std::vector<Vector3> positions;     // 固定位置スポーン座標（Carrier など、splineId 不使用時）
	int         count          = 1;     // スポーン数（positions が空の場合は同一地点に count 体）
	float       shootIntervalT = 0.04f; // 射撃間隔 [t]（Drone 用。0 で射撃なし）
	float       spawnIntervalSec = 5.0f;// 子敵生成間隔 [秒]（Carrier 用）
	int         spawnLimit     = 4;     // 同時出現上限（Carrier 用）
	std::string childPrefab;            // 子敵プレハブ名（Carrier 用。空なら自身のプレハブにフォールバック）
	std::string childSplineId;          // 子敵を乗せるスプライン名（Carrier 用。空なら自身のスプラインにフォールバック）
};

/// <summary>
/// ステージ1本分のスポーン定義。
/// </summary>
struct WaveDef {
	std::string name;
	std::vector<WaveEntry> entries;
};

namespace WaveDefIO {
	bool LoadFromFile(const std::string& filePath, WaveDef& out);
	bool SaveToFile(const std::string& filePath, const WaveDef& def);
}
