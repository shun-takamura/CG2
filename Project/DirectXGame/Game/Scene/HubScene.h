#pragma once
#include "BaseScene.h"
#include <memory>

class Camera;

/// <summary>
/// ハブシーン（メインメニュー）
/// タブUIで「ステージセレクト / スキルショップ / スキル装備 / タイトル / 終了」を切り替える
/// シーン遷移を挟まずタブ切替でビューを切り替える設計。
/// </summary>
class HubScene : public BaseScene {
public:
	enum class Tab {
		StageSelect,
		SkillShop,
		SkillEquip,
		BackToTitle,
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
