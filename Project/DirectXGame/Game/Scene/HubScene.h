#pragma once
#include "GameScene.h"
#include "VerticalMenu.h"
#include <memory>

class Camera;

/// <summary>
/// ハブシーン（ステージ選択を兼ねる）
/// 「Stage1 / タイトルに戻る」を上下キー+決定で選ぶ。ステージが増えたら項目を足す。
/// ゲーム終了はタイトルの責務に一本化している。
/// </summary>
class HubScene : public GameScene {
public:
	HubScene();
	~HubScene() override;

	void Initialize() override;
	void Finalize() override;
	void Update() override;
	void Draw() override;

	Camera* GetCamera() override;

private:
	std::unique_ptr<Camera> camera_;
	VerticalMenu menu_;
};
