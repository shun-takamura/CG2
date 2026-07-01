#pragma once

#include "Prefab.h" // MovementType

/// <summary>
/// 敵プレハブ用の移動パラメータ（登場/移動の方法と速度）。
/// Inspector で編集 → "Save as Prefab" でプレハブ JSON（movement セクション）に反映される。
/// enemyType（WaveEntry）が攻撃ロールを決めるのに対し、こちらは「どう登場し・どう動くか」を決める。
/// </summary>
struct MovementParams {
	bool         enabled            = false;
	MovementType movementType       = MovementType::SplineFollow;
	float        moveSpeed          = 10.0f;  // SplineFollow の基準速度 / Drift の徘徊速度 [units/sec]
	float        hoverApproachSpeed = 30.0f;  // ScreenHover: 飛来速度 [units/sec]
	float        hoverHoldDuration  = 6.0f;   // ScreenHover: 停止して攻撃を続ける時間（経過で逃走）[sec]
};
