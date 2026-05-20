#pragma once
#include "Vector3.h"

class IImGuiEditable;
class BaseScene;

/// <summary>
/// コマンドの Update に渡すコンテキスト。
/// コマンドは out 系フィールドに書き込み、EnemyController が毎フレーム吸収する。
/// </summary>
struct EnemyContext {
	// --- in ---
	IImGuiEditable* player        = nullptr;
	BaseScene*      scene         = nullptr;
	float           railT         = 0.0f;   // 現在のカメラ進行度 t
	float           triggerT      = 0.0f;   // このエントリの trigger_t（射撃間隔計算基準）
	float           shootIntervalT = 0.04f; // 射撃間隔 [t]
	float           spawnIntervalSec = 5.0f; // 子スポーン間隔 [秒]（Carrier 用）
	int             spawnLimit    = 4;      // 子スポーン上限（Carrier 用）
	std::string     childPrefab;            // 運び屋が生成する子プレハブ名
	std::string     childSplineId;          // 子スポーン先スプライン名

	// --- out (コマンドが書き込み、EnemyController が読む) ---
	bool    requestDetach    = false;          // スプライン追従から切り離す
	Vector3 freeVelocity     { 0.0f, 0.0f, 0.0f }; // 切り離し後の自由移動速度
	bool    useFreeVelocity  = false;
	bool    billboardToPlayer = true;          // false にするとビルボードを無効化
};
