#pragma once
#include "BaseScene.h"
#include <memory>
#include <vector>
#include <string>
#include <random>
#include "Camera.h" 
#include "Object3DInstance.h"

// 前方宣言
class SpriteInstance;

/// <summary>
/// タイトルシーン
/// </summary>
class TitleScene : public BaseScene {
public:
	~TitleScene() override;

	void Initialize() override;
	void Finalize() override;
	void Update() override;
	void Draw() override;

private:

	// パーティクル用乱数
	std::mt19937 randomEngine_;
	int particleTimer_ = 0;

	std::unique_ptr<Object3DInstance> title_;
	std::unique_ptr<Object3DInstance> space_;

	// カメラ
	std::unique_ptr<Camera> camera_;
};