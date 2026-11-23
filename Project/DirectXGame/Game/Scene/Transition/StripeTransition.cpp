#include "StripeTransition.h"
#include "SpriteManager.h"
#include "SpriteInstance.h"
#include "DirectXCore.h"
#include "TextureManager.h"
#include "MathUtility.h"
#include <algorithm>
#include <cmath>
#include <imgui.h>

namespace {
	// 1600x900 で目視調整した帯の幅 [px]
	constexpr float kDesignStripeWidth = 890.0f;
	// 帯の大きさを目視調整したときの画面の幅 [px]
	constexpr float kDesignScreenWidth = 1600.0f;
	// 1600x900 で目視調整した帯の高さ [px]
	constexpr float kDesignStripeHeight = 1000.0f;
	// 帯の大きさを目視調整したときの画面の高さ [px]
	constexpr float kDesignScreenHeight = 900.0f;

	// transitionDuration_ を 0 にされても割り算が壊れない下限 [秒]
	constexpr float kMinTransitionDuration = 0.001f;
}

void StripeTransition::OnInitialize() {

	// 帯のサイズも 1600x900 で目視調整した値だったので、比率で画面に追従させる。
	// ImGui から手動調整もできるので、ここは初期値の算出だけ。
	debugStripeWidth_  = GetScreenWidth()  * (kDesignStripeWidth / kDesignScreenWidth);
	debugStripeHeight_ = GetScreenHeight() * (kDesignStripeHeight / kDesignScreenHeight);

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
	TextureManager::GetInstance()->LoadTexture("Resources/Textures/stripe.dds");

	for (int i = 0; i < stripeCount_; ++i) {
		auto stripe = std::make_unique<SpriteInstance>();

		std::string spriteName = "StripeTransition_" + std::to_string(i);
		stripe->Initialize(GetSpriteManager(), "Resources/Textures/stripe.dds", spriteName);

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

void StripeTransition::OnStartCover() {
	if (needsRecreate_) {
		CreateStripes();
	}

	for (size_t i = 0; i < stripeProgress_.size(); ++i) {
		stripeProgress_[i] = 0.0f;
	}
}

float StripeTransition::CalcStripeProgress(size_t index) const {
	// i 本目は stripeDelay_ × i 秒遅れて動き始め、transitionDuration_ 秒で 1 になる
	const float startTime = stripeDelay_ * static_cast<float>(index);
	const float duration = (std::max)(transitionDuration_, kMinTransitionDuration);
	return std::clamp((GetPhaseTime() - startTime) / duration, 0.0f, 1.0f);
}

bool StripeTransition::UpdateCover([[maybe_unused]] float dt) {
	bool allComplete = true;
	for (size_t i = 0; i < stripes_.size(); ++i) {
		stripeProgress_[i] = CalcStripeProgress(i);
		if (stripeProgress_[i] < 1.0f) {
			allComplete = false;
		}
	}
	return allComplete;
}

bool StripeTransition::UpdateReveal([[maybe_unused]] float dt) {
	// 覆っている状態（1）から、ずらしながら 0 へ戻す
	bool allGone = true;
	for (size_t i = 0; i < stripes_.size(); ++i) {
		stripeProgress_[i] = 1.0f - CalcStripeProgress(i);
		if (stripeProgress_[i] > 0.0f) {
			allGone = false;
		}
	}
	return allGone;
}

void StripeTransition::SetFullyCovered() {
	if (needsRecreate_) {
		CreateStripes();
	}
	for (size_t i = 0; i < stripeProgress_.size(); ++i) {
		stripeProgress_[i] = 1.0f;
	}
}

void StripeTransition::UpdateVisual() {
	// デバッグ強制表示中は常に更新
	if (!IsTransitioning() && !debugForceShow_) return;

	// 帯の位置を更新
	UpdateStripePositions();

	// スプライトの更新
	for (size_t i = 0; i < stripes_.size(); ++i) {
		stripes_[i]->Update();
	}
}

void StripeTransition::UpdateStripePositions() {
	if (stripes_.empty()) return;

	float spacing = GetScreenWidth() / static_cast<float>(stripes_.size());

	// 斜めに掃く量。もとは 1600x900 で目視調整した固定値だったので、
	// その比率のまま画面サイズに追従させる。
	const float kSweepXRatio  = 600.0f / kDesignScreenWidth;  // 開始 X のオフセット
	const float kTravelYRatio = 870.0f / kDesignScreenHeight;   // はけるときの Y 移動量
	const float kEnterYRatio  = 200.0f / kDesignScreenHeight;   // 入りの Y 助走
	const float kBaseY        = -50.0f / kDesignScreenHeight;   // 覆っている状態の Y

	const float sweepX  = GetScreenWidth()  * kSweepXRatio;
	const float travelY = GetScreenHeight() * kTravelYRatio;
	const float enterY  = GetScreenHeight() * kEnterYRatio;
	const float baseY   = GetScreenHeight() * kBaseY;
	// 下へ 1 動くごとに横へ動く量（stripeAngle_ が負なら左へ）
	const float slopeX = std::tan(DegToRad(stripeAngle_));

	for (size_t i = 0; i < stripes_.size(); ++i) {
		float progress = stripeProgress_[i];

		if (debugForceShow_ && !IsTransitioning()) {
			progress = 1.0f;
		}

		float x, y;

		if (GetState() == TransitionState::FadeOut ||
			(GetState() == TransitionState::None && !IsTransitioning())) {
			// はけ：覆っている状態から左下へ
			float startX = (spacing + spacingOffset_) * i - sweepX;
			float startY = baseY;
			float endY = startY + travelY;
			float endX = startX + (endY - startY) * slopeX;

			float outProgress = 1.0f - progress;
			x = startX + (endX - startX) * outProgress;
			y = startY + (endY - startY) * outProgress;
		} else {
			// 入り：右上から左下へ（はけと同じ向きに動く）
			float endX = (spacing + spacingOffset_) * i - sweepX;
			float endY = baseY;
			float startY = -GetScreenHeight() - enterY;
			float startX = endX - (endY - startY) * slopeX;

			x = startX + (endX - startX) * progress;
			y = startY + (endY - startY) * progress;
		}

		stripePositions_[i] = { x, y };
		stripes_[i]->SetSize({ debugStripeWidth_, debugStripeHeight_ });
		stripes_[i]->SetPosition({ x, y });
	}
}

void StripeTransition::Draw() {
	// スプライト描画設定
	GetSpriteManager()->DrawSetting();

	// 全部の帯を描く（画面の外にいる帯は何も映らない）
	for (size_t i = 0; i < stripes_.size(); ++i) {
		stripes_[i]->Draw();
	}
}

void StripeTransition::OnImGui() {
#ifdef _DEBUG

	{
		ImGui::Text("=== Status ===");
		ImGui::Text("IsTransitioning: %s", IsTransitioning() ? "true" : "false");
		ImGui::Text("State: %d (0=None, 1=FadeIn, 2=Hold, 3=FadeOut)", static_cast<int>(GetState()));
		ImGui::Text("CurrentTime: %.2f", GetPhaseTime());
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
		ImGui::Text("=== Size Settings ===");

		bool changed = false;

		changed |= ImGui::SliderFloat("Stripe Width", &debugStripeWidth_, 50.0f, 1500.0f);
		changed |= ImGui::SliderFloat("Stripe Height", &debugStripeHeight_, 100.0f, 2000.0f);

		if (changed && debugForceShow_) {
			UpdateStripePositions();
			for (size_t i = 0; i < stripes_.size(); ++i) {
				stripes_[i]->Update();
			}
		}

		ImGui::Separator();
		ImGui::Text("=== Timing Settings ===");

		ImGui::SliderFloat("Duration", &transitionDuration_, 0.05f, 2.0f);
		ImGui::SliderFloat("Delay", &stripeDelay_, 0.0f, 0.3f);
		ImGui::SliderFloat("Angle (deg)", &stripeAngle_, -60.0f, 60.0f);
		{ float hold = GetHoldDuration(); if (ImGui::SliderFloat("Hold Duration", &hold, 0.0f, 1.0f)) SetHoldDuration(hold); }

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
			ResetToIdle();
			Start(nullptr); // シーン切り替えなし（差し替えるシーンを渡さない）
			debugForceShow_ = false;
		}

		ImGui::SameLine();

		if (ImGui::Button("Reset")) {
			for (size_t i = 0; i < stripeProgress_.size(); ++i) {
				stripeProgress_[i] = 0.0f;
			}
			ResetToIdle();
			debugForceShow_ = false;
		}
	}

#endif // _DEBUG
}