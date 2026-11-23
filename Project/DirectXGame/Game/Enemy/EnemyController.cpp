#include "EnemyController.h"

void EnemyController::Init(IImGuiEditable* entity, int waveEntryIndex, bool billboardToPlayer,
	const EnemyWaveParams& params, std::vector<std::unique_ptr<IEnemyCommand>> cmds) {
	entity_            = entity;
	waveEntryIndex_    = waveEntryIndex;
	billboardToPlayer_ = billboardToPlayer;
	params_            = params;
	commands_          = std::move(cmds);
	commandIdx_        = 0;
	entered_           = false;
}

void EnemyController::Update(float dt, EnemyContext& ctx) {
	// Wave のエントリから渡された値とシーンからの通知（in）
	ctx.triggerSec         = params_.triggerSec;
	ctx.shootIntervalSec   = params_.shootIntervalSec;
	ctx.spawnIntervalSec   = params_.spawnIntervalSec;
	ctx.spawnLimit         = params_.spawnLimit;
	ctx.childPrefab        = params_.childPrefab;
	ctx.childSplineId      = params_.childSplineId;
	ctx.hoverOffset        = params_.hoverOffset;
	ctx.hoverApproachSpeed = params_.hoverApproachSpeed;
	ctx.hoverHoldDuration  = params_.hoverHoldDuration;
	ctx.splineArrived      = splineArrived_;

	// 前フレームの out 値をコンテキストに引き継ぐ
	ctx.requestDetach     = requestDetach_;
	ctx.freeVelocity      = freeVelocity_;
	ctx.useFreeVelocity   = useFreeVelocity_;
	ctx.billboardToPlayer = billboardToPlayer_;
	// 接触ダメージは「そのフレームにコマンドが立てたか」だけを見たいので毎フレームリセット
	ctx.contactDamageActive = false;

	if (commandIdx_ < commands_.size()) {
		auto& cmd = commands_[commandIdx_];
		if (!entered_) {
			cmd->OnEnter(entity_, ctx);
			entered_ = true;
		}
		cmd->Update(dt, entity_, ctx);
		if (cmd->IsFinished()) {
			cmd->OnExit(entity_, ctx);
			commandIdx_++;
			entered_ = false;
		}
	}

	// コマンドが書き込んだ out 値を保持
	requestDetach_       = ctx.requestDetach;
	freeVelocity_        = ctx.freeVelocity;
	useFreeVelocity_     = ctx.useFreeVelocity;
	billboardToPlayer_   = ctx.billboardToPlayer;
	contactDamageActive_ = ctx.contactDamageActive;
}

void EnemyController::TriggerRetreat() {
	if (commands_.empty()) return;
	AdvanceTo(commands_.size() - 1);
}

bool EnemyController::IsDone() const {
	return commandIdx_ >= commands_.size();
}

bool EnemyController::ConsumeDetachRequest() {
	const bool requested = requestDetach_;
	requestDetach_ = false;
	return requested;
}

void EnemyController::AdvanceTo(size_t idx) {
	if (idx >= commands_.size()) return;
	if (idx == commandIdx_ && entered_) return;
	commandIdx_ = idx;
	entered_    = false;
}
