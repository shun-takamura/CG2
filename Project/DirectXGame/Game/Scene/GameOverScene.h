#pragma once
#include "GameScene.h"
#include <memory>
#include <vector>

class Camera;

/// <summary>
/// ゲームオーバーシーン
/// 「ボス戦からリトライ」（ボス戦到達済みの場合のみ）「シューティングからリトライ」
/// 「ステージセレクトに戻る」を選択式メニューで表示する。
/// </summary>
class GameOverScene : public GameScene {
public:
	enum class Option {
		BossRetry,
		RailRetry,
		BackToStageSelect,
	};

	GameOverScene();
	~GameOverScene() override;

	void Initialize() override;
	void Finalize() override;
	void Update() override;
	void Draw() override;

	Camera* GetCamera() override;

private:
	std::unique_ptr<Camera> camera_;
	std::vector<Option> options_;
	int selectedIndex_ = 0;
};
