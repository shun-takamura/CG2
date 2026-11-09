#pragma once
#include "GameScene.h"
#include "Vector4.h"
#include <memory>
#include <string>
#include <vector>

class Camera;

/// <summary>
/// リザルトシーン
/// ScoreManager からスコア・撃破数、LootLedger から手に入れたアイテムとエーテルの合計（22_ItemDrop.md）を取得して表示し、
/// ENTERでステージセレクトへ戻る。報酬の数は 1 行ずつ数え上がる（決定で数え上げを飛ばし、もう一度で戻る）。
/// </summary>
class ResultScene : public GameScene {
public:
	ResultScene();
	~ResultScene() override;

	void Initialize() override;
	void Finalize() override;
	void Update() override;
	void Draw() override;

	Camera* GetCamera() override;

private:
	std::unique_ptr<Camera> camera_;

	int score_ = 0;
	int killCount_ = 0;

	// 獲得した報酬（Initialize で集計を写す。最後の行がエーテル）
	struct RewardRow {
		std::string label;
		Vector4 color{ 1.0f, 1.0f, 1.0f, 1.0f };
		int count = 0;
		bool ether = false;
	};
	std::vector<RewardRow> rewards_;
	float time_ = 0.0f;   // 出てからの実時間 [s]

	float CountUpEndTime() const;
};
