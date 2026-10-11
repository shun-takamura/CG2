#include "FadeTransition.h"
#include "SpriteManager.h"
#include "SpriteInstance.h"
#include "DirectXCore.h"
#include <algorithm>

void FadeTransition::Initialize(SpriteManager* spriteManager, DirectXCore* dxCore,
	float screenWidth, float screenHeight) {
	spriteManager_ = spriteManager;
	dxCore_ = dxCore;
	screenWidth_ = screenWidth;
	screenHeight_ = screenHeight;

	// フェード用スプライト作成
	fadeSprite_ = std::make_unique<SpriteInstance>();
	fadeSprite_->Initialize(spriteManager_, "Resources/Textures/white1x1.dds", "FadeTransition");
	fadeSprite_->SetPosition({ 0.0f, 0.0f });
	fadeSprite_->SetSize({ screenWidth_, screenHeight_ });
	fadeSprite_->SetColor({ color_.x, color_.y, color_.z, 0.0f }); // 最初は透明
}

void FadeTransition::SetNextFade(const Vector3& color, float fadeDuration, float holdDuration) {
	color_ = color;
	fadeDuration_ = fadeDuration;
	holdDuration_ = holdDuration;
}

void FadeTransition::Finalize() {
	fadeSprite_.reset();
}

void FadeTransition::OnStartCover() {
	alpha_ = 0.0f;
}

bool FadeTransition::UpdateCover(float /*dt*/) {
	// フェードイン：α値を0→1（currentTime_ は基底が進める）
	alpha_ = (std::min)(currentTime_ / fadeDuration_, 1.0f);
	return alpha_ >= 1.0f;
}

bool FadeTransition::UpdateReveal(float /*dt*/) {
	// フェードアウト：α値を1→0
	alpha_ = (std::max)(1.0f - currentTime_ / fadeDuration_, 0.0f);
	return alpha_ <= 0.0f;
}

void FadeTransition::SetFullyCovered() {
	alpha_ = 1.0f;
}

void FadeTransition::UpdateVisual() {
	if (!isTransitioning_) return;
	fadeSprite_->SetColor({ color_.x, color_.y, color_.z, alpha_ });
	fadeSprite_->Update();
}

void FadeTransition::OnFinished() {
	alpha_ = 0.0f;
	// SetNextFade の指定はこの1回だけ
	color_ = { 0.0f, 0.0f, 0.0f };
	fadeDuration_ = kDefaultFadeDuration;
	holdDuration_ = kDefaultHoldDuration;
}

void FadeTransition::Draw() {
	// トランジション中でなければ描画しない
	if (!isTransitioning_ && state_ == TransitionState::None) return;

	// α値が0なら描画しない
	if (alpha_ <= 0.0f) return;

	// スプライト描画
	spriteManager_->DrawSetting();
	fadeSprite_->Draw();
}
