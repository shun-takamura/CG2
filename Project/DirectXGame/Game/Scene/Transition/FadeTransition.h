#pragma once
#include "BaseTransition.h"
#include <memory>

class SpriteInstance;

/// <summary>
/// シンプルなフェードイン/アウトトランジション
/// </summary>
class FadeTransition : public BaseTransition {
public:
	FadeTransition() = default;
	~FadeTransition() override = default;

	void Initialize(SpriteManager* spriteManager, DirectXCore* dxCore,
		float screenWidth = 1280.0f, float screenHeight = 720.0f) override;

	void Finalize() override;
	void Update() override;
	void Draw() override;
	void Start(std::function<void()> onSceneChange) override;

	TransitionType GetType() const override { return TransitionType::Fade; }
	std::string GetName() const override { return "Fade"; }

	void SetFadeDuration(float duration) { fadeDuration_ = duration; }

private:
	std::unique_ptr<SpriteInstance> fadeSprite_;
	float fadeDuration_ = 0.5f;
	float alpha_ = 0.0f;
};
