#pragma once
#include "GameScene.h"
#include <memory>

class Camera;

/// <summary>
/// ハブシーン（メインメニュー）
/// 「StageSelect / ゲーム終了」の2択を上下キー+決定で選ぶ。
/// スキルショップ等の追加タブは将来ここに拡張する想定。
/// </summary>
class HubScene : public GameScene {
public:
	enum class Tab {
		StageSelect,
		Quit,
	};

	HubScene();
	~HubScene() override;

	void Initialize() override;
	void Finalize() override;
	void Update() override;
	void Draw() override;

	Camera* GetCamera() override;

private:
	std::unique_ptr<Camera> camera_;
	Tab currentTab_ = Tab::StageSelect;
};
