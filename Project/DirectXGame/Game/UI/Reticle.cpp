#include "Reticle.h"

#include "SpriteInstance.h"

Reticle::Reticle() = default;
Reticle::~Reticle() = default;

#include "MouseInput.h"
#include "ControllerInput.h"
#include "WindowsApplication.h"

#include <algorithm>
#include <cmath>
#include <array>

void Reticle::Initialize(SpriteManager* spriteManager, const std::string& texturePath) {
	sprite_ = std::make_unique<SpriteInstance>();
	sprite_->Initialize(spriteManager, texturePath, "Reticle");
	sprite_->SetAnchorPoint({ 0.5f, 0.5f });
	sprite_->SetSize({ sizePx_, sizePx_ });

	currentSizePx_ = sizePx_;

	// 外側パーツ（4枚）を初期化
	// アンカーポイント (0.0, 1.0) = 左下基準。各パーツに 90 度ずつ回転を設定して 4 方向に配置する
	for (int i = 0; i < 4; ++i) {
		outerParticles_[i] = std::make_unique<SpriteInstance>();
		outerParticles_[i]->Initialize(spriteManager, "Resources/Textures/reticle_outside.dds", "ReticleOuter" + std::to_string(i));
		outerParticles_[i]->SetAnchorPoint({ 0.0f, 1.0f });
		outerParticles_[i]->SetSize({ lockOnMinPxOutside_, lockOnMinPxOutside_ });//仮で最小値で置く
		// 0°/90°/180°/270° (rad) で各パーツの向きを固定
		outerParticles_[i]->SetRotation(i * 3.14159265f * 0.5f);
	}

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

	// ===== チャージアニメーション =====
	if (chargeLevel_ >= 0.0f) {
		// 内側レティクルのサイズ範囲（lockOnMinPx_ ～ lockOnMaxPx_）を 0～1 に正規化
		// 非ロックオン時 = 0、ロックオンで敵が近いほど 1 に近づく
		const float denom = (lockOnMaxPx_ - lockOnMinPx_);
		const float t = (denom > 1e-4f)
			? std::clamp((currentSizePx_ - lockOnMinPx_) / denom, 0.0f, 1.0f)
			: 0.0f;

		// チャージ完了直後の追加広がり（startRadius → 0 へ減衰）
		float extraSpread = 0.0f;
		if (outerChargeEasingElapsed_ < outerChargeEasingDuration_) {
			outerChargeEasingElapsed_ += deltaTime;
			float et = std::clamp(outerChargeEasingElapsed_ / outerChargeEasingDuration_, 0.0f, 1.0f);
			extraSpread = outerChargeEasingStart_ * (1.0f - et);
		}

		// オフセット = min→max を拡大率で補間 + チャージ演出の広がり
		const float offset = lockOnMinPxOutside_ + t * (lockOnMaxPxOutside_ - lockOnMinPxOutside_) + extraSpread;

		// 4 方向（右上 / 右下 / 左下 / 左上）にアンカーポイントを配置
		// 各パーツの回転は Initialize 時に 0°/90°/180°/270° で固定済み
		const Vector2 anchorOffsets[4] = {
			{ +offset, -offset }, // パーツ 0: 中心の右上
			{ +offset, +offset }, // パーツ 1: 中心の右下
			{ -offset, +offset }, // パーツ 2: 中心の左下
			{ -offset, -offset }, // パーツ 3: 中心の左上
		};

		// スプライトサイズ = min→max を拡大率で補間
		const float outerSize = outerSizeMinPx_ + t * (outerSizeMaxPx_ - outerSizeMinPx_);
		for (int i = 0; i < 4; ++i) {
			Vector2 partPos{ position_.x + anchorOffsets[i].x, position_.y + anchorOffsets[i].y };
			outerParticles_[i]->SetPosition(partPos);
			outerParticles_[i]->SetSize({ outerSize, outerSize });
			outerParticles_[i]->SetColor(lockedOn_ ? lockOnColor_ : normalColor_);
			outerParticles_[i]->Update();
		}
	}
}

void Reticle::Draw() {
	if (sprite_) sprite_->Draw();

	// チャージ中なら外側パーツも描画
	if (chargeLevel_ >= 0.0f) {
		for (int i = 0; i < 4; ++i) {
			if (outerParticles_[i]) outerParticles_[i]->Draw();
		}
	}
}

void Reticle::SetLockOn(bool on) {
	lockedOn_ = on;
	if (sprite_) {
		sprite_->SetColor(lockedOn_ ? lockOnColor_ : normalColor_);
	}
}

void Reticle::StartChargeAnimation(float startRadius, float endRadius, float duration) {
	chargeLevel_ = 0.0f;
	outerRadius_ = startRadius;
	outerChargeEasingStart_ = startRadius;
	outerChargeEasingEnd_ = endRadius;
	outerChargeEasingDuration_ = duration > 0.0f ? duration : 0.3f;
	outerChargeEasingElapsed_ = 0.0f;
	outerRotation_ = 0.0f;

	// 即座に最新の position_ で各パーツの位置を反映（前回フレームの古い位置で1フレーム描画される問題を回避）
	// 内側レティクルのサイズ範囲を 0～1 に正規化
	const float denom = (lockOnMaxPx_ - lockOnMinPx_);
	const float t = (denom > 1e-4f)
		? std::clamp((currentSizePx_ - lockOnMinPx_) / denom, 0.0f, 1.0f)
		: 0.0f;
	// オフセット = min→max 補間 + チャージ開始時の広がり（startRadius）
	const float offset = lockOnMinPxOutside_ + t * (lockOnMaxPxOutside_ - lockOnMinPxOutside_) + startRadius;
	const Vector2 anchorOffsets[4] = {
		{ +offset, -offset },
		{ +offset, +offset },
		{ -offset, +offset },
		{ -offset, -offset },
	};
	// サイズ = min→max 補間
	const float outerSize = outerSizeMinPx_ + t * (outerSizeMaxPx_ - outerSizeMinPx_);
	for (int i = 0; i < 4; ++i) {
		if (!outerParticles_[i]) continue;
		Vector2 partPos{ position_.x + anchorOffsets[i].x, position_.y + anchorOffsets[i].y };
		outerParticles_[i]->SetPosition(partPos);
		outerParticles_[i]->SetSize({ outerSize, outerSize });
		outerParticles_[i]->SetColor(lockedOn_ ? lockOnColor_ : normalColor_);
		outerParticles_[i]->Update();
	}
}

void Reticle::EndChargeAnimation() {
	chargeLevel_ = -1.0f;
	outerChargeEasingElapsed_ = 0.0f;
}
