#include "FadeTransition.h"
#include "SpriteManager.h"
#include "SpriteInstance.h"
#include "DirectXCore.h"

void FadeTransition::Initialize(SpriteManager* spriteManager, DirectXCore* dxCore,
	float screenWidth, float screenHeight) {
	spriteManager_ = spriteManager;
	dxCore_ = dxCore;
	screenWidth_ = screenWidth;
	screenHeight_ = screenHeight;

	// フェード用スプライト作成
	fadeSprite_ = std::make_unique<SpriteInstance>();
	fadeSprite_->Initialize(spriteManager_, "Resources/white1x1.png", "FadeTransition");
	fadeSprite_->SetPosition({ 0.0f, 0.0f });
	fadeSprite_->SetSize({ screenWidth_, screenHeight_ });
	fadeSprite_->SetColor({ 0.0f, 0.0f, 0.0f, 0.0f }); // 黒、最初は透明
}

void FadeTransition::Finalize() {
	fadeSprite_.reset();
}

void FadeTransition::Start(std::function<void()> onSceneChange) {
	alpha_ = 0.0f;
	currentTime_ = 0.0f;
	BaseTransition::Start(onSceneChange);
}

void FadeTransition::Update() {
	if (!isTransitioning_) return;

	// 1フレーム分の時間を加算
	currentTime_ += 1.0f / 60.0f;

	// 状態に応じた処理
	if (state_ == TransitionState::FadeIn) {
		// フェードイン：α値を0→1
		alpha_ = currentTime_ / fadeDuration_;

		// 1.0でクランプ
		if (alpha_ >= 1.0f) {
			alpha_ = 1.0f;
			state_ = TransitionState::Hold;
			currentTime_ = 0.0f;
		}
	} else if (state_ == TransitionState::Hold) {
		// シーン切り替えを実行
		if (!sceneChangeTriggered_ && onSceneChange_) {
			onSceneChange_();
			sceneChangeTriggered_ = true;
		}

		// 少し待ってからフェードアウトへ
		if (currentTime_ >= holdDuration_) {
			state_ = TransitionState::FadeOut;
			currentTime_ = 0.0f;
		}
	} else if (state_ == TransitionState::FadeOut) {
		// フェードアウト：α値を1→0
		alpha_ = 1.0f - (currentTime_ / fadeDuration_);

		// 0でクランプ
		if (alpha_ <= 0.0f) {
			alpha_ = 0.0f;
			state_ = TransitionState::None;
			isTransitioning_ = false;
		}
	}

	// α値をスプライトに適用
	fadeSprite_->SetColor({ 0.0f, 0.0f, 0.0f, alpha_ });
	fadeSprite_->Update();
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