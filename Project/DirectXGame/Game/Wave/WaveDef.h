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
	float       triggerSec = 0.0f; // 出現秒（ステージ開始からの経過秒）
	float       retreatSec = -1.0f;// 退避秒（-1 = なし、突進型など消滅で終わるタイプ）
	float       traverseSec = 8.0f;// 敵スプラインを踏破するのにかかる秒数
	std::string splineId;               // 移動スプライン名（Drone の固定ルート / Rusher の登場スプライン）
	std::vector<Vector3> positions;     // 固定位置スポーン座標（Carrier など、splineId 不使用時）
	int         count          = 1;     // スポーン数（positions が空の場合は同一地点に count 体）
	float       shootIntervalSec = 3.0f;// 射撃間隔 [秒]（Drone 用。0 で射撃なし）
	float       spawnIntervalSec = 5.0f;// 子敵生成間隔 [秒]（Carrier 用）
	int         spawnLimit     = 4;     // 同時出現上限（Carrier 用）
	std::string childPrefab;            // 子敵プレハブ名（Carrier 用。空なら自身のプレハブにフォールバック）
	std::string childSplineId;          // 子敵を乗せるスプライン名（Carrier 用。空なら自身のスプラインにフォールバック）

	// ScreenHover / Static 用：トリガー時のカメラを基準にしたローカルオフセット
	// （x=右 / y=上 / z=前方深度）。実行時 world = camPos + camRot*cameraOffset で
	// 配置され、カメラが進んでも画面上の同じ位置に止まって見える。
	bool        useCameraOffset = false;
	Vector3     cameraOffset{ 0.0f, 0.0f, 30.0f };
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
