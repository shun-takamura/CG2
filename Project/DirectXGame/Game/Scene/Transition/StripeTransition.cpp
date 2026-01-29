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

	// stripe.pngのメタデータを取得してテクスチャサイズを取得
	const DirectX::TexMetadata& metadata =
		TextureManager::GetInstance()->GetMetaData("Resources/stripe.png");
	float texWidth = static_cast<float>(metadata.width);
	float texHeight = static_cast<float>(metadata.height);

	for (int i = 0; i < stripeCount_; ++i) {
		auto stripe = std::make_unique<SpriteInstance>();

		// 斜めの画像を使用
		stripe->Initialize(spriteManager_, "Resources/stripe.png", "StripeTransition_" + std::to_string(i));

		// テクスチャ全体を使用するように設定（重要！）
		stripe->SetTextureLeftTop({ 0.0f, 0.0f });
		stripe->SetTextureSize({ texWidth, texHeight });

		// スプライトのサイズ設定
		stripe->SetSize({ debugStripeWidth_, debugStripeHeight_ });

		// 左上を基準点に
		stripe->SetAnchorPoint({ 0.0f, 0.0f });

		// 回転なし（画像自体が斜め）
		stripe->SetRotation(0.0f);

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

			for (int i = 0; i < stripeCount_; ++i) {
				float startTime = i * stripeDelay_;

				if (currentTime_ >= startTime) {
					float elapsed = currentTime_ - startTime;
					stripeProgress_[i] = elapsed / transitionDuration_;

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

			for (int i = 0; i < stripeCount_; ++i) {
				float startTime = i * stripeDelay_;

				if (currentTime_ >= startTime) {
					float elapsed = currentTime_ - startTime;
					stripeProgress_[i] = 1.0f - (elapsed / transitionDuration_);

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
	float spacing = screenWidth_ / stripeCount_;

	for (int i = 0; i < stripeCount_; ++i) {
		// X位置
		float x = spacing * i + debugOffsetX_;

		// Y位置：進行度に応じて移動
		float progress = stripeProgress_[i];

		// デバッグ強制表示時は progress = 1.0 として扱う
		if (debugForceShow_ && !isTransitioning_) {
			progress = 1.0f;
		}

		float y = debugStartY_ + (debugEndY_ - debugStartY_) * progress;

		// 位置を保存（ImGui表示用）
		stripePositions_[i] = { x, y };

		// サイズも更新
		stripes_[i]->SetSize({ debugStripeWidth_, debugStripeHeight_ });
		stripes_[i]->SetPosition({ x, y });
	}
}

void StripeTransition::Draw() {
	// 通常時またはデバッグ強制表示時に描画
	bool shouldDraw = (isTransitioning_ && state_ != TransitionState::None) || debugForceShow_;
	if (!shouldDraw) return;

	// スプライト描画設定
	spriteManager_->DrawSetting();

	// 描画
	for (int i = 0; i < stripeCount_; ++i) {
		// デバッグ強制表示または進行度が0より大きい場合に描画
		if (debugForceShow_ || stripeProgress_[i] > 0.0f) {
			stripes_[i]->Draw();
		}
	}
}

void StripeTransition::OnImGui() {
	if (ImGui::Begin("Stripe Transition Debug")) {

		ImGui::Text("=== Status ===");
		ImGui::Text("IsTransitioning: %s", isTransitioning_ ? "true" : "false");
		ImGui::Text("State: %d (0=None, 1=FadeIn, 2=Hold, 3=FadeOut)", static_cast<int>(state_));
		ImGui::Text("CurrentTime: %.2f", currentTime_);

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
			}
		}

		ImGui::Separator();
		ImGui::Text("=== Stripe Positions ===");

		for (int i = 0; i < stripeCount_ && i < static_cast<int>(stripePositions_.size()); ++i) {
			ImGui::Text("Stripe[%d]: pos=(%.1f, %.1f), progress=%.2f",
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
}