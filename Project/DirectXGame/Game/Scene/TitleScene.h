#pragma once
#include "GameScene.h"
#include <memory>

class Camera;

/// <summary>
/// タイトルシーン
/// SPACE/Enter で Hub へ遷移。
/// 一定時間無操作でデモプレイ動画を再生する仕様（未実装）
/// </summary>
class TitleScene : public GameScene {
public:
	TitleScene();
	~TitleScene() override;

	void Initialize() override;
	void Finalize() override;
	void Update() override;
	void Draw() override;

	Camera* GetCamera() override;

private:
	std::unique_ptr<Camera> camera_;

	// 無操作タイマー（秒）。閾値を超えたらデモ動画再生に遷移する予定
	float idleSeconds_ = 0.0f;
	static constexpr float kDemoTriggerSeconds = 10.0f;
};
