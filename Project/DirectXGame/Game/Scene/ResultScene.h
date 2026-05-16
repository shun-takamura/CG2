#pragma once
#include "BaseScene.h"
#include <memory>

class Camera;

/// <summary>
/// リザルトシーン
/// スコア・タイムを集計し、スキルポイント(SP)を獲得して Hub へ戻る
/// </summary>
class ResultScene : public BaseScene {
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

	// 仮の集計値（後でステージ結果を受け取る仕組みにする）
	int score_ = 0;
	int gainedSP_ = 0;
};
