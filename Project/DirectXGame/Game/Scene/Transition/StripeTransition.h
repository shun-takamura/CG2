#pragma once
#include "BaseTransition.h"
#include "SpriteInstance.h"
#include <vector>
#include <memory>

class SpriteInstance;

/// <summary>
/// スマブラ風斜めストライプトランジション
/// </summary>
class StripeTransition : public BaseTransition {
public:
	StripeTransition() = default;
	~StripeTransition() override = default;

	void Initialize(SpriteManager* spriteManager, DirectXCore* dxCore,
		float screenWidth = 1280.0f, float screenHeight = 720.0f) override;
	void Finalize() override;
	void Update() override;
	void Draw() override;
	void Start(std::function<void()> onSceneChange) override;

	TransitionType GetType() const override { return TransitionType::Stripe; }
	std::string GetName() const override { return "Stripe"; }

	// カスタマイズ用セッター
	void SetStripeCount(int count) { stripeCount_ = count; needsRecreate_ = true; }
	void SetTransitionDuration(float duration) { transitionDuration_ = duration; }
	void SetStripeAngle(float angle) { stripeAngle_ = angle; needsRecreate_ = true; }
	void SetStripeDelay(float delay) { stripeDelay_ = delay; }

	/// <summary>
	/// ImGuiでデバッグ表示・調整
	/// </summary>
	void OnImGui();

private:
	void CreateStripes();
	void UpdateStripePositions();

	// 帯の設定
	int stripeCount_ = 8;
	float stripeAngle_ = -30.0f;
	float stripeWidth_ = 0.0f;
	float transitionDuration_ = 0.5f;
	float stripeDelay_ = 0.05f;

	float spacingOffset_ = 50.0f;
	float moveSpeed_ = 0.1f;

	// 再生成フラグ
	bool needsRecreate_ = false;

	// 各帯のスプライト
	std::vector<std::unique_ptr<SpriteInstance>> stripes_;

	// 各帯の進行度（0.0〜1.0）
	std::vector<float> stripeProgress_;

	// デバッグ用：各帯の位置を保存
	std::vector<Vector2> stripePositions_;

	// ImGui調整用パラメータ
	float debugStartY_ = -1020.0f;
	float debugEndY_ = -100.0f;
	float debugOffsetX_ = -50.0f;
	float debugStripeWidth_ = 210.0f;
	float debugStripeHeight_ = 920.0f;
	bool debugForceShow_ = false;  // 強制表示フラグ
};