#include "Reticle.h"

#include "SpriteInstance.h"

Reticle::Reticle() = default;
Reticle::~Reticle() = default;

#include "MouseInput.h"
#include "ControllerInput.h"
#include "WindowsApplication.h"

#include <algorithm>
#include <cmath>

void Reticle::Initialize(SpriteManager* spriteManager, const std::string& texturePath) {
	sprite_ = std::make_unique<SpriteInstance>();
	sprite_->Initialize(spriteManager, texturePath, "Reticle");
	sprite_->SetAnchorPoint({ 0.5f, 0.5f });
	sprite_->SetSize({ sizePx_, sizePx_ });

	currentSizePx_ = sizePx_;

	ResetToCenter();
}

void Reticle::ResetToCenter() {
	position_ = {
		static_cast<float>(WindowsApplication::kClientWidth) * 0.5f,
		static_cast<float>(WindowsApplication::kClientHeight) * 0.5f,
	};
}

void Reticle::Update(bool mouseHovered, const Vector2& mouseClient, bool mouseMoved,
	ControllerInput* pad, float deltaTime)
{
	if (!sprite_) return;

	// マウスが Viewport の上にあり、かつこのフレームに動いた → そこへワープ
	if (mouseHovered && mouseMoved) {
		position_ = mouseClient;
	}

	// 右スティックで相対移動（傾き量 × スピード）。コントローラーは ImGui と衝突しない
	if (pad && pad->IsConnected()) {
		const auto& rs = pad->GetRightStick();
		if (rs.magnitude > 0.0f) {
			position_.x += rs.x * rs.magnitude * stickSpeed_ * deltaTime;
			// 画面 Y は下向き正のため、スティック上 (rs.y +) を画面上 (Y-) にマップ
			position_.y -= rs.y * rs.magnitude * stickSpeed_ * deltaTime;
		}
	}

	// 中心が画面外に出ないようにクランプ
	const float maxX = static_cast<float>(WindowsApplication::kClientWidth);
	const float maxY = static_cast<float>(WindowsApplication::kClientHeight);
	position_.x = std::clamp(position_.x, 0.0f, maxX);
	position_.y = std::clamp(position_.y, 0.0f, maxY);

	// サイズを通常 ↔ ロックオン時の「敵の見かけサイズ」へ Lerp 追従
	float targetSize = sizePx_;
	if (lockedOn_) {
		targetSize = std::clamp(lockOnTargetSizePx_, lockOnMinPx_, lockOnMaxPx_);
	}
	if (sizeSmoothTime_ > 1e-4f) {
		const float alpha = 1.0f - std::exp(-deltaTime / sizeSmoothTime_);
		currentSizePx_ += (targetSize - currentSizePx_) * alpha;
	} else {
		currentSizePx_ = targetSize;
	}

	sprite_->SetPosition(position_);
	sprite_->SetSize({ currentSizePx_, currentSizePx_ });
	sprite_->Update();
}

void Reticle::Draw() {
	if (sprite_) sprite_->Draw();
}

void Reticle::SetLockOn(bool on) {
	lockedOn_ = on;
	if (sprite_) {
		sprite_->SetColor(lockedOn_ ? lockOnColor_ : normalColor_);
	}
}
