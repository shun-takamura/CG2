#pragma once
#include "Vector3.h"

class IImGuiEditable;
class GameScene;

/// <summary>
/// コマンドの Update に渡すコンテキスト。
/// コマンドは out 系フィールドに書き込み、EnemyController が毎フレーム吸収する。
/// </summary>
struct EnemyContext {
	// --- in ---
	IImGuiEditable* player        = nullptr;
	GameScene*      scene         = nullptr;
	float           stageTimeSec  = 0.0f;   // ステージ開始からの経過秒（スポーン/射撃判定の基準）
	bool            splineArrived = false;  // スプライン終端到達済み（Rusher の溜め開始判定用）
	float           triggerSec    = 0.0f;   // このエントリの出現秒（射撃間隔計算の起点）
	float           shootIntervalSec = 3.0f;// 射撃間隔 [秒]（0で射撃なし）
	float           spawnIntervalSec = 5.0f; // 子スポーン間隔 [秒]（Carrier 用）
	int             spawnLimit    = 4;      // 子スポーン上限（Carrier 用）
	std::string     childPrefab;            // 運び屋が生成する子プレハブ名
	std::string     childSplineId;          // 子スポーン先スプライン名

	// ScreenHover（画面内停止型）用
	Vector3         hoverOffset{ 0.0f, 0.0f, 30.0f }; // カメラローカルの停止オフセット（右/上/前）
	float           hoverApproachSpeed = 30.0f;       // 飛来速度 [units/sec]
	float           hoverHoldDuration  = 6.0f;        // 停止して攻撃を続ける時間 [sec]

	// --- out (コマンドが書き込み、EnemyController が読む) ---
	bool    requestDetach    = false;          // スプライン追従から切り離す
	Vector3 freeVelocity     { 0.0f, 0.0f, 0.0f }; // 切り離し後の自由移動速度
	bool    useFreeVelocity  = false;
	bool    billboardToPlayer = true;          // false にするとビルボードを無効化
	// 突進など「攻撃判定を持つ接触」中のみ true（ただの移動接触はダメージ対象外にするため）。
	// EnemyController が毎フレーム false にリセットし、コマンドがそのフレームだけ true を立てる。
	bool    contactDamageActive = false;
};
