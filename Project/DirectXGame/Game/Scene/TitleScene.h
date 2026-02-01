#pragma once
#include <memory>
#include <vector>
#include "BaseScene.h"

// 前方宣言
class SpriteInstance;

/// <summary>
/// タイトルシーン
/// </summary>
class TitleScene : public BaseScene {
public:
	TitleScene();
	~TitleScene() override;

	void Initialize() override;
	void Finalize() override;
	void Update() override;
	void Draw() override;

private:
	// スプライトをvectorで管理
	std::vector<std::unique_ptr<SpriteInstance>> sprites_;
};