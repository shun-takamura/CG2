#include "GameSession.h"

#include "BossRetryState.h"
#include "DifficultyProfile.h"
#include "EtherState.h"
#include "LoadoutState.h"
#include "LootLedger.h"
#include "ScoreManager.h"
#include "StageClearRecord.h"

GameSession::GameSession()
	: etherState_(std::make_unique<EtherState>()),
	  difficultyProfile_(std::make_unique<DifficultyProfile>(etherState_.get())),
	  lootLedger_(std::make_unique<LootLedger>(difficultyProfile_.get())),
	  loadoutState_(std::make_unique<LoadoutState>()),
	  scoreManager_(std::make_unique<ScoreManager>()),
	  bossRetryState_(std::make_unique<BossRetryState>()),
	  stageClearRecord_(std::make_unique<StageClearRecord>()) {
}

GameSession::~GameSession() = default;
