#include "StripeTransition.h"
#include "SpriteManager.h"
#include "SpriteInstance.h"
#include "DirectXCore.h"
#include "TextureManager.h"
#include <cmath>
#include <imgui.h>

void StripeTransition::Initialize(SpriteManager* spriteManager, DirectXCore* dxCore,
	float screenWidth, float screenHeight) {
	spriteManager_ = spriteManager;
	dxCore_ = dxCore;
	screenWidth_ = screenWidth;
	screenHeight_ = screenHeight;

	CreateStripes();
}

void StripeTransition::Finalize() {
	stripes_.clear();
	stripeProgress_.clear();
	stripePositions_.clear();
}

void StripeTransition::CreateStripes() {
	stripes_.clear();
	stripeProgress_.clear();
	stripePositions_.clear();

	// stripe.pngを先に読み込む
	TextureManager::GetInstance()->LoadTexture("Resources/stripe.png");

	for (int i = 0; i < stripeCount_; ++i) {
		auto stripe = std::make_unique<SpriteInstance>();

		std::string spriteName = "StripeTransition_" + std::to_string(i);
		stripe->Initialize(spriteManager_, "Resources/stripe.png", spriteName);

		// サイズを先に設定（重要！）
		stripe->SetSize({ debugStripeWidth_, debugStripeHeight_ });

		// 位置は後で設定
		stripe->SetPosition({ 0.0f, 0.0f });

		// 左上を基準点に
		stripe->SetAnchorPoint({ 0.0f, 0.0f });

		stripes_.push_back(std::move(stripe));
		stripeProgress_.push_back(0.0f);
		stripePositions_.push_back({ 0.0f, 0.0f });
	}

	needsRecreate_ = false;
}

void StripeTransition::Start(std::function<void()> onSceneChange) {
	if (needsRecreate_) {
		CreateStripes();
	}

	for (size_t i = 0; i < stripeProgress_.size(); ++i) {
		stripeProgress_[i] = 0.0f;
	}
	currentTime_ = 0.0f;

	BaseTransition::Start(onSceneChange);
}

void StripeTransition::Update() {
	// デバッグ強制表示中は常に更新
	if (!isTransitioning_ && !debugForceShow_) return;

	// 通常のトランジション処理
	if (isTransitioning_) {
		currentTime_ += 1.0f / 60.0f;

		if (state_ == TransitionState::FadeIn) {
			bool allComplete = true;
			float triggerY = debugStripeHeight_ / 2.0f;

			for (size_t i = 0; i < stripes_.size(); ++i) {
				bool shouldStart = false;

				if (i == 0) {
					shouldStart = true;
				} else {
					float prevTriggerPoint = stripePositions_[i - 1].y + (debugStripeHeight_ * 2.0f / 3.0f);
					if (prevTriggerPoint >= triggerY) {
						shouldStart = true;
					}
				}

				if (shouldStart && stripeProgress_[i] < 1.0f) {
					stripeProgress_[i] += moveSpeed_;
					if (stripeProgress_[i] > 1.0f) {
						stripeProgress_[i] = 1.0f;
					}
				}

				if (stripeProgress_[i] < 1.0f) {
					allComplete = false;
				}
			}

			if (allComplete) {
				state_ = TransitionState::Hold;
				currentTime_ = 0.0f;
			}

		} else if (state_ == TransitionState::Hold) {
			if (!sceneChangeTriggered_ && onSceneChange_) {
				onSceneChange_();
				sceneChangeTriggered_ = true;
			}

			if (currentTime_ >= holdDuration_) {
				state_ = TransitionState::FadeOut;
				currentTime_ = 0.0f;
			}

		} else if (state_ == TransitionState::FadeOut) {
			bool allGone = true;

			for (size_t i = 0; i < stripes_.size(); ++i) {
				bool shouldStart = false;

				if (i == 0) {
					shouldStart = true;
				} else {
					if (stripeProgress_[i - 1] <= 0.7f) {
						shouldStart = true;
					}
				}

				if (shouldStart && stripeProgress_[i] > 0.0f) {
					stripeProgress_[i] -= moveSpeed_;
					if (stripeProgress_[i] < 0.0f) {
						stripeProgress_[i] = 0.0f;
					}
				}

				if (stripeProgress_[i] > 0.0f) {
					allGone = false;
				}
			}

			if (allGone) {
				state_ = TransitionState::None;
				isTransitioning_ = false;
			}
		}
	}

	// 帯の位置を更新
	UpdateStripePositions();

	// スプライトの更新
	for (size_t i = 0; i < stripes_.size(); ++i) {
		stripes_[i]->Update();
	}
}

void StripeTransition::UpdateStripePositions() {
	if (stripes_.empty()) return;

	float spacing = screenWidth_ / static_cast<float>(stripes_.size());

	for (size_t i = 0; i < stripes_.size(); ++i) {
		float progress = stripeProgress_[i];

		if (debugForceShow_ && !isTransitioning_) {
			progress = 1.0f;
		}

		float x, y;

		if (state_ == TransitionState::FadeOut ||
			(state_ == TransitionState::None && !isTransitioning_)) {
			// はけ：覆っている状態から左下へ
			float startX = (spacing + spacingOffset_) * i - 450.0f;
			float startY = -50.0f;
			float endX = startX - 500.0f;
			float endY = startY + 870.0f;

			float outProgress = 1.0f - progress;
			x = startX + (endX - startX) * outProgress;
			y = startY + (endY - startY) * outProgress;
		} else {
			// 入り：右上から左下へ
			float startX = (spacing + spacingOffset_) * i + 50.0f;
			float startY = -screenHeight_ - 200.0f;
			float endX = (spacing + spacingOffset_) * i - 450.0f;
			float endY = -50.0f;

			x = startX + (endX - startX) * progress;
			y = startY + (endY - startY) * progress;
		}

		stripePositions_[i] = { x, y };
		stripes_[i]->SetSize({ debugStripeWidth_, debugStripeHeight_ });
		stripes_[i]->SetPosition({ x, y });
	}
}

void StripeTransition::Draw() {
	// デバッグ：常に描画する
	OutputDebugStringA("StripeTransition::Draw() - FORCE DRAWING\n");

	// スプライト描画設定
	spriteManager_->DrawSetting();

	// 強制的に全て描画
	for (size_t i = 0; i < stripes_.size(); ++i) {
		stripes_[i]->Draw();
	}
}

void StripeTransition::OnImGui() {
#ifdef DEBUG

	if (ImGui::Begin("Stripe Transition Debug")) {

		ImGui::Text("=== Status ===");
		ImGui::Text("IsTransitioning: %s", isTransitioning_ ? "true" : "false");
		ImGui::Text("State: %d (0=None, 1=FadeIn, 2=Hold, 3=FadeOut)", static_cast<int>(state_));
		ImGui::Text("CurrentTime: %.2f", currentTime_);
		ImGui::Text("Stripe Count: %zu", stripes_.size());

		ImGui::Separator();
		ImGui::Text("=== Debug Controls ===");

		// 強制表示トグル
		if (ImGui::Checkbox("Force Show Stripes", &debugForceShow_)) {
			if (debugForceShow_) {
				// 強制表示をONにしたら位置を更新
				for (size_t i = 0; i < stripeProgress_.size(); ++i) {
					stripeProgress_[i] = 1.0f;
				}
				UpdateStripePositions();
				// スプライトも更新
				for (size_t i = 0; i < stripes_.size(); ++i) {
					stripes_[i]->Update();
				}
			}
		}

		ImGui::Separator();
		ImGui::Text("=== Position Settings ===");

		bool changed = false;
		changed |= ImGui::SliderFloat("Start Y", &debugStartY_, -2000.0f, 0.0f);
		changed |= ImGui::SliderFloat("End Y", &debugEndY_, -500.0f, 500.0f);
		changed |= ImGui::SliderFloat("Offset X", &debugOffsetX_, -200.0f, 200.0f);

		ImGui::Separator();
		ImGui::Text("=== Size Settings ===");

		changed |= ImGui::SliderFloat("Stripe Width", &debugStripeWidth_, 50.0f, 500.0f);
		changed |= ImGui::SliderFloat("Stripe Height", &debugStripeHeight_, 100.0f, 2000.0f);

		if (changed && debugForceShow_) {
			UpdateStripePositions();
			for (size_t i = 0; i < stripes_.size(); ++i) {
				stripes_[i]->Update();
			}
		}

		ImGui::Separator();
		ImGui::Text("=== Timing Settings ===");

		ImGui::SliderFloat("Duration", &transitionDuration_, 0.1f, 2.0f);
		ImGui::SliderFloat("Delay", &stripeDelay_, 0.01f, 0.2f);
		ImGui::SliderFloat("Hold Duration", &holdDuration_, 0.0f, 1.0f);

		ImGui::Separator();
		ImGui::Text("=== Stripe Count ===");

		int count = stripeCount_;
		if (ImGui::SliderInt("Count", &count, 1, 16)) {
			stripeCount_ = count;
			needsRecreate_ = true;
			CreateStripes();
			if (debugForceShow_) {
				for (size_t i = 0; i < stripeProgress_.size(); ++i) {
					stripeProgress_[i] = 1.0f;
				}
				UpdateStripePositions();
				for (size_t i = 0; i < stripes_.size(); ++i) {
					stripes_[i]->Update();
				}
			}
		}

		ImGui::Separator();
		ImGui::Text("=== Stripe Positions ===");

		for (size_t i = 0; i < stripes_.size() && i < stripePositions_.size(); ++i) {
			ImGui::Text("Stripe[%zu]: pos=(%.1f, %.1f), progress=%.2f",
				i, stripePositions_[i].x, stripePositions_[i].y, stripeProgress_[i]);
		}

		ImGui::Separator();
		ImGui::Text("=== Manual Test ===");

		if (ImGui::Button("Start Transition (Test)")) {
			// テスト用にトランジション開始（シーン切り替えなし）
			for (size_t i = 0; i < stripeProgress_.size(); ++i) {
				stripeProgress_[i] = 0.0f;
			}
			currentTime_ = 0.0f;
			state_ = TransitionState::FadeIn;
			isTransitioning_ = true;
			sceneChangeTriggered_ = true; // シーン切り替えはスキップ
			debugForceShow_ = false;
		}

		ImGui::SameLine();

		if (ImGui::Button("Reset")) {
			for (size_t i = 0; i < stripeProgress_.size(); ++i) {
				stripeProgress_[i] = 0.0f;
			}
			currentTime_ = 0.0f;
			state_ = TransitionState::None;
			isTransitioning_ = false;
			debugForceShow_ = false;
		}
	}
	ImGui::End();

#endif // DEBUG
}