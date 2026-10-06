#pragma once
#include "BaseTransition.h"
#include "Vector3.h"
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
		float screenWidth, float screenHeight) override;

	void Finalize() override;
	void Update() override;
	void Draw() override;
	void Start(std::function<void()> onSceneChange) override;

	TransitionType GetType() const override { return TransitionType::Fade; }
	std::string GetName() const override { return "Fade"; }

	void SetFadeDuration(float duration) { fadeDuration_ = duration; }

	/// <summary>
	/// 次の1回だけ色と長さを変える（タイトルの扉に入る白フェード等）。
	/// 遷移が終わると黒・既定の長さに戻るので、他のシーンのフェードには影響しない。
	/// ChangeScene(..., TransitionType::Fade) の直前に呼ぶ。
	/// </summary>
	void SetNextFade(const Vector3& color, float fadeDuration, float holdDuration);

private:
	static constexpr float kDefaultFadeDuration = 0.5f;
	static constexpr float kDefaultHoldDuration = 0.1f;

	std::unique_ptr<SpriteInstance> fadeSprite_;
	float fadeDuration_ = kDefaultFadeDuration;
	float alpha_ = 0.0f;
	Vector3 color_ = { 0.0f, 0.0f, 0.0f };
};
