#pragma once
#include <memory>
#include "Components/Gameplay.h"
#include "Enemy/Commands/IEnemyCommand.h"
#include "Enemy/EnemyContext.h"
#include "Enemy/EnemyController.h"
#include "Commands/WanderInScreenCommand.h"
#include "Commands/RetreatCommand.h"
#include "IImGuiEditable.h"
#include "Scene/GameScene.h"

/// <summary>
/// 運び屋（Carrier）用：一定間隔で子敵を生成する。
/// 子敵は運び屋の位置から出現し、徘徊→寿命到達で退避するコマンド列が割り当てられる。
/// 子敵の寿命・徘徊半径・移動速度は親運び屋の CarrierParams から取得する。
/// </summary>
class SpawnDroneCommand : public IEnemyCommand {
public:
	/// <summary>タイマーと出した数を 0 に戻す</summary>
	/// <param name="entity">動かす敵（このコマンドでは使わない）</param>
	/// <param name="ctx">敵の入力と結果の書き込み先（このコマンドでは使わない）</param>
	void OnEnter([[maybe_unused]] IImGuiEditable* entity, [[maybe_unused]] EnemyContext& ctx) override {
		timer_      = 0.0f;
		spawnCount_ = 0;
	}

	/// <summary>出現間隔ごとに運び屋の位置へ子敵を出し、徘徊→退避のコントローラを付けてシーンに登録する（上限まで）</summary>
	/// <param name="dt">経過時間 [秒]</param>
	/// <param name="entity">運び屋</param>
	/// <param name="ctx">敵の入力（子のプレハブ・間隔・上限）と結果の書き込み先</param>
	void Update(float dt, IImGuiEditable* entity, EnemyContext& ctx) override {
		if (!ctx.scene || ctx.childPrefab.empty()) return;
		if (spawnCount_ >= ctx.spawnLimit) return;

		timer_ += dt;
		if (timer_ < ctx.spawnIntervalSec) return;
		timer_ -= ctx.spawnIntervalSec;

		// 運び屋の現在位置にスポーン
		Vector3 spawnPos{ 0.0f, 0.0f, 0.0f };
		if (entity) {
			if (const Vector3* p = entity->GetEditableTranslate()) spawnPos = *p;
		}
		IImGuiEditable* child = ctx.scene->SpawnEnemyAt(ctx.childPrefab, spawnPos);
		if (!child) return;
		spawnCount_++;

		// CarrierParams から子のパラメータを取得（運び屋 = entity）
		float lifetime = kDefaultChildLifetime, radius = kDefaultChildWanderRadius, speed = kDefaultChildMoveSpeed;
		if (entity) {
			const CarrierParams& cp = Gameplay::Of(entity).GetCarrierParams();
			if (cp.enabled) {
				lifetime = cp.childLifetimeSec;
				radius   = cp.childWanderRadius;
				speed    = cp.childMoveSpeed;
			}
		}

		// 子敵専用 EnemyController を作成し、Wander → Retreat のコマンド列を割り当てる
		EnemyWaveParams params;
		params.triggerSec       = ctx.stageTimeSec;     // 子の出現時刻 = 親の現在秒
		params.shootIntervalSec = ctx.shootIntervalSec; // 親の射撃間隔をそのまま継承
		std::vector<std::unique_ptr<IEnemyCommand>> cmds;
		cmds.push_back(std::make_unique<WanderInScreenCommand>(entity, radius, lifetime, speed));
		cmds.push_back(std::make_unique<RetreatCommand>());
		auto ctrl = std::make_unique<EnemyController>();
		ctrl->Init(child, -1, true, params, std::move(cmds));
		ctx.scene->RegisterEnemyController(std::move(ctrl));
	}

	/// <summary>コマンドが終わったか（TriggerRetreat で外から中断するので終わらない）</summary>
	/// <returns>常に false</returns>
	bool IsFinished() const override { return false; }

private:
	// 運び屋に CarrierParams が無いときの子の寿命 [秒]（CarrierParams の初期値と同じ）
	static constexpr float kDefaultChildLifetime = 10.0f;
	// 運び屋に CarrierParams が無いときの子の徘徊半径 [m]
	static constexpr float kDefaultChildWanderRadius = 8.0f;
	// 運び屋に CarrierParams が無いときの子の移動の速さ [m/秒]
	static constexpr float kDefaultChildMoveSpeed = 5.0f;
	float timer_      = 0.0f;
	int   spawnCount_ = 0;
};
