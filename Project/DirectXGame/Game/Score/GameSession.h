#pragma once
#include <memory>

class EtherState;
class DifficultyProfile;
class LootLedger;
class LoadoutState;
class ScoreManager;
class BossRetryState;
class StageClearRecord;

/// <summary>
/// 1 回の起動（プレイ）の間、シーンをまたいで残る状態をまとめて持つ。
/// Game が 1 つだけ持ち、SceneManager がシーンを作るたびに GameScene::SetGameSession で渡す。
/// シーンの中の部品（RailStagePart・FlightUnitPart・LootDrops など）へは、持ち主のシーンや host を通して渡す。
/// どこからでも書き換えられるシングルトンにせず、持ち主と渡し先をはっきりさせるために、この形にしている。
/// **セーブはしない**（ゲームを起動している間だけ）。
/// </summary>
class GameSession {
public:
	/// <summary>中の状態をすべて初めの値で作る（EtherState・DifficultyProfile は調整値の JSON を読む）</summary>
	GameSession();
	/// <summary>中の状態を作った順の逆に壊す</summary>
	~GameSession();
	GameSession(const GameSession&) = delete;
	GameSession& operator=(const GameSession&) = delete;

	/// <summary>エーテル（手元・賭け分・純度）を取得する</summary>
	/// <returns>エーテルの状態</returns>
	EtherState* GetEtherState() const { return etherState_.get(); }
	/// <summary>純度から決まる敵の強さの倍率を取得する</summary>
	/// <returns>難易度の倍率</returns>
	DifficultyProfile* GetDifficultyProfile() const { return difficultyProfile_.get(); }
	/// <summary>そのステージで手に入れた物の集計を取得する</summary>
	/// <returns>手に入れた物の集計</returns>
	LootLedger* GetLootLedger() const { return lootLedger_.get(); }
	/// <summary>いま付けている装備を取得する</summary>
	/// <returns>装備の状態</returns>
	LoadoutState* GetLoadoutState() const { return loadoutState_.get(); }
	/// <summary>ステージ中のスコアを取得する</summary>
	/// <returns>スコア</returns>
	ScoreManager* GetScoreManager() const { return scoreManager_.get(); }
	/// <summary>ボス戦からのリトライに使う状態を取得する</summary>
	/// <returns>ボス戦のリトライの状態</returns>
	BossRetryState* GetBossRetryState() const { return bossRetryState_.get(); }
	/// <summary>クリアした回の記録（クリアタイム・撃破の瞬間のキャプチャ）を取得する</summary>
	/// <returns>クリアの記録</returns>
	StageClearRecord* GetStageClearRecord() const { return stageClearRecord_.get(); }

private:
	// 作る順に並べる（DifficultyProfile は EtherState を、LootLedger は DifficultyProfile を読むので、その後ろ）
	std::unique_ptr<EtherState> etherState_;
	std::unique_ptr<DifficultyProfile> difficultyProfile_;
	std::unique_ptr<LootLedger> lootLedger_;
	std::unique_ptr<LoadoutState> loadoutState_;
	std::unique_ptr<ScoreManager> scoreManager_;
	std::unique_ptr<BossRetryState> bossRetryState_;
	std::unique_ptr<StageClearRecord> stageClearRecord_;
};
