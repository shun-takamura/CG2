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
/// BaseScene::enemyControllers_ に格納され、毎フレーム Update が呼ばれる。
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
	float       triggerT_        = 0.0f;
	float       shootIntervalT_  = 0.04f;
	float       spawnIntervalSec_ = 5.0f;
	int         spawnLimit_      = 4;
	std::string childPrefab_;
	std::string childSplineId_;

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
