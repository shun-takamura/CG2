#pragma once
#include "GameScene.h"
#include <memory>

class Camera;

/// <summary>
/// ステージセレクトシーン
/// まだステージ1しかないため、"Stage1" とだけ表示してENTERキーで直接開始する。
/// </summary>
class StageSelectScene : public GameScene {
public:
	StageSelectScene();
	~StageSelectScene() override;

	void Initialize() override;
	void Finalize() override;
	void Update() override;
	void Draw() override;

	Camera* GetCamera() override;

private:
	std::unique_ptr<Camera> camera_;
};
