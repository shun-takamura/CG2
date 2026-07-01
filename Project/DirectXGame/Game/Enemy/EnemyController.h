#pragma once
#include <vector>
#include <memory>
#include <string>
#include "Vector3.h"
#include "IEnemyCommand.h"
#include "EnemyContext.h"

class IImGuiEditable;

/// <summary>
/// 敵1体分の AI コマンド列を管理するコントローラ。
/// GameScene::enemyControllers_ に格納され、毎フレーム Update が呼ばれる。
/// </summary>
class EnemyController {
public:
	IImGuiEditable* entity_         = nullptr;
	int             waveEntryIndex_ = -1;

	// MovingEnemy がスプライン終端 (t>=1.0) に到達したら true。Rusher の溜め開始トリガー等に使う。
	bool    splineArrived_  = false;

	// EnemyContext の out フィールドをフレーム間で保持するバッファ
	bool    requestDetach_   = false;
	Vector3 freeVelocity_    { 0.0f, 0.0f, 0.0f };
	bool    useFreeVelocity_ = false;
	bool    billboardToPlayer_ = true;
	bool    contactDamageActive_ = false; // 突進など攻撃接触中のみ true（被弾判定側が参照）

	// WaveEntry から設定するパラメータ（UpdateEnemyControllers が ctx にセットする）
	float       triggerSec_      = 0.0f;   // 出現秒（ステージ開始基準）
	float       shootIntervalSec_ = 3.0f;  // 射撃間隔 [秒]
	float       spawnIntervalSec_ = 5.0f;
	int         spawnLimit_      = 4;
	std::string childPrefab_;
	std::string childSplineId_;

	// ScreenHover（画面内停止型）用パラメータ
	Vector3     hoverOffset_{ 0.0f, 0.0f, 30.0f };
	float       hoverApproachSpeed_ = 30.0f;
	float       hoverHoldDuration_  = 6.0f;

	void Init(std::vector<std::unique_ptr<IEnemyCommand>> cmds);

	/// <summary>
	/// 毎フレーム呼ぶ。ctx の in フィールドを設定してから渡すこと。
	/// </summary>
	void Update(float dt, EnemyContext& ctx);

	/// <summary>
	/// 最終コマンド（Retreat 想定）に強制ジャンプ。retreat_t 到達時に呼ぶ。
	/// </summary>
	void TriggerRetreat();

	/// <summary>
	/// 全コマンドが終了しているか。
	/// </summary>
	bool IsDone() const;

private:
	std::vector<std::unique_ptr<IEnemyCommand>> commands_;
	size_t commandIdx_ = 0;
	bool   entered_    = false;

	void AdvanceTo(size_t idx);
};
