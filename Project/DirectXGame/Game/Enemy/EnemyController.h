#pragma once
#include <vector>
#include <memory>
#include <string>
#include "Vector3.h"
#include "Commands/IEnemyCommand.h"
#include "EnemyContext.h"

class IImGuiEditable;

/// <summary>
/// Wave のエントリから敵 1 体へ渡す値（出現秒・射撃・子の出現・画面内停止）。
/// コントローラはこれを毎フレーム EnemyContext に詰めてコマンドへ渡す。
/// </summary>
struct EnemyWaveParams {
	float       triggerSec        = 0.0f;  // 出現秒（ステージ開始基準）
	float       shootIntervalSec  = 3.0f;  // 射撃間隔 [秒]（0 で射撃なし）
	float       spawnIntervalSec  = 5.0f;  // 子の出現間隔 [秒]（運び屋用）
	int         spawnLimit        = 4;     // 子の出現上限（運び屋用）
	std::string childPrefab;               // 運び屋が出す子のプレハブ名
	std::string childSplineId;             // 子が乗るスプライン名
	Vector3     hoverOffset{ 0.0f, 0.0f, 30.0f }; // 画面内停止型：カメラローカルの停止オフセット（右/上/前）
	float       hoverApproachSpeed = 30.0f;        // 画面内停止型：飛来速度 [units/秒]
	float       hoverHoldDuration  = 6.0f;         // 画面内停止型：停止して攻撃を続ける時間 [秒]
};

/// <summary>
/// 敵1体分の AI コマンド列を管理するコントローラ。
/// GameScene::enemyControllers_ に格納され、毎フレーム Update が呼ばれる。
/// コマンドが書いた結果（切り離し・自由移動・ビルボード・攻撃接触）は読み取り専用で公開する。
/// </summary>
class EnemyController {
public:
	/// <summary>敵とコマンド列を設定する（コマンドは先頭から順に実行する）</summary>
	/// <param name="entity">動かす敵</param>
	/// <param name="waveEntryIndex">Wave のエントリ番号（Wave 経由でなければ -1）</param>
	/// <param name="billboardToPlayer">最初から自機の方を向かせるか（コマンドが途中で切り替えられる）</param>
	/// <param name="params">Wave のエントリから渡す値</param>
	/// <param name="cmds">実行するコマンド列</param>
	void Init(IImGuiEditable* entity, int waveEntryIndex, bool billboardToPlayer,
		const EnemyWaveParams& params, std::vector<std::unique_ptr<IEnemyCommand>> cmds);

	/// <summary>今のコマンドを 1 フレーム進める。Wave の値とスプライン終端の通知は自分で ctx に詰める</summary>
	/// <param name="dt">World の経過秒</param>
	/// <param name="ctx">シーン側が入れた値（自機・時刻・視錐台・偏差射撃）。コマンドの結果もここに書かれる</param>
	void Update(float dt, EnemyContext& ctx);

	/// <summary>最終コマンド（Retreat 想定）に強制ジャンプする。retreat_t 到達時に呼ぶ</summary>
	void TriggerRetreat();

	/// <summary>全コマンドが終了しているか</summary>
	/// <returns>終了していれば true</returns>
	bool IsDone() const;

	/// <summary>動かしている敵（破棄されたら nullptr）</summary>
	/// <returns>敵</returns>
	IImGuiEditable* GetEntity() const { return entity_; }

	/// <summary>敵が破棄されたので手放す（以後は何もしない）</summary>
	void ClearEntity() { entity_ = nullptr; }

	/// <summary>Wave のエントリ番号</summary>
	/// <returns>番号（Wave 経由でなければ -1）</returns>
	int GetWaveEntryIndex() const { return waveEntryIndex_; }

	/// <summary>スプラインの終端に着いたことを知らせる（突進の溜め開始などのきっかけ）</summary>
	void NotifySplineArrived() { splineArrived_ = true; }

	/// <summary>自機の方を向かせるか（コマンドが切り替える）</summary>
	/// <returns>向かせるなら true</returns>
	bool IsBillboardToPlayer() const { return billboardToPlayer_; }

	/// <summary>突進など攻撃判定を持つ接触の最中か（このフレームにコマンドが立てたときだけ true）</summary>
	/// <returns>攻撃接触中なら true</returns>
	bool IsContactDamageActive() const { return contactDamageActive_; }

	/// <summary>スプラインから切り離した後の自由移動を使うか</summary>
	/// <returns>使うなら true</returns>
	bool UsesFreeVelocity() const { return useFreeVelocity_; }

	/// <summary>自由移動の速度（ワールド）</summary>
	/// <returns>速度</returns>
	const Vector3& GetFreeVelocity() const { return freeVelocity_; }

	/// <summary>スプラインからの切り離しの要求を受け取る（受け取ったら要求は消える）</summary>
	/// <returns>要求があれば true</returns>
	bool ConsumeDetachRequest();

private:
	/// <summary>指定した番号のコマンドへ進める（OnEnter は次の Update で呼ぶ）</summary>
	/// <param name="idx">コマンドの番号</param>
	void AdvanceTo(size_t idx);

	IImGuiEditable* entity_         = nullptr;
	int             waveEntryIndex_ = -1;
	EnemyWaveParams params_;

	std::vector<std::unique_ptr<IEnemyCommand>> commands_;
	size_t commandIdx_ = 0;
	bool   entered_    = false;

	// シーンから知らされた値（次の Update で ctx に詰める）
	bool    splineArrived_ = false;
	// コマンドが ctx に書いた結果をフレーム間で保持する
	bool    requestDetach_       = false;
	Vector3 freeVelocity_        { 0.0f, 0.0f, 0.0f };
	bool    useFreeVelocity_     = false;
	bool    billboardToPlayer_   = true;
	bool    contactDamageActive_ = false;
};
