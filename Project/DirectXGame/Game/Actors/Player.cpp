#include "Player.h"
#include "Object3DManager.h"
#include "Object3DInstance.h"
#include "DirectXCore.h"
#include "InputManager.h"
#include "KeyboardInput.h"
#include "Bullet.h"
#include "ParticleManager.h"
#include <cmath>
#include <algorithm>
#include <cstdlib>

Player::Player() {}

Player::~Player() {}

void Player::Initialize(
	Object3DManager* object3DManager,
	DirectXCore* dxCore,
	const std::string& modelName,
	const std::string& bulletType,
	const std::string& bulletMode
) {
	object3DManager_ = object3DManager;
	dxCore_ = dxCore;
	bulletType_ = bulletType;
	bulletMode_ = bulletMode;

	model_ = std::make_unique<Object3DInstance>();
	model_->Initialize(
		object3DManager_,
		dxCore_,
		"Resources",
		modelName,
		"Player"
	);

	position_ = { -5.0f, 0.0f, 0.0f };
	model_->SetTranslate(position_);
	model_->SetRotate({ 0.0f, 3.14f, 0.0f });

	hp_ = maxHp_;
	isAlive_ = true;
	fireTimer_ = 0.0f;
	damageFlashTimer_ = 0.0f;
	isCharging_ = false;
	chargeTime_ = 0.0f;
}

// =============================================================
// Update
// =============================================================
void Player::Update(InputManager* input, float deltaTime) {
	if (!isAlive_) {
		return;
	}

	Move(input, deltaTime);

	// 弾モードで分岐
	if (bulletMode_ == "charge") {
		ShootCharge(input, deltaTime);
	} else {
		ShootNormal(input, deltaTime);
	}

	UpdateBullets(deltaTime);

	// 被弾フラッシュ
	if (damageFlashTimer_ > 0.0f) {
		damageFlashTimer_ -= deltaTime;
		if (damageFlashTimer_ < 0.0f) {
			damageFlashTimer_ = 0.0f;
		}
	}

	model_->SetTranslate(position_);
	model_->SetRotate(rotation_);
	model_->Update();
}

// =============================================================
// Draw
// =============================================================
void Player::Draw(DirectXCore* dxCore) {
	if (!isAlive_) {
		return;
	}

	model_->Draw(dxCore);

	for (const auto& bullet : bullets_) {
		bullet->Draw(dxCore);
	}
}

// =============================================================
// TakeDamage
// =============================================================
void Player::TakeDamage(int amount) {
	if (!isAlive_) {
		return;
	}

	hp_ -= amount;
	damageFlashTimer_ = damageFlashDuration_;

	if (hp_ <= 0) {
		hp_ = 0;
		isAlive_ = false;
	}
}

// =============================================================
// GetDamageRatio
// =============================================================
float Player::GetDamageRatio() const {
	if (maxHp_ <= 0) return 1.0f;
	return 1.0f - static_cast<float>(hp_) / static_cast<float>(maxHp_);
}

// =============================================================
// GetChargeRatio
// =============================================================
float Player::GetChargeRatio() const {
	if (chargeMaxTime_ <= 0.0f) return 0.0f;
	return std::clamp(chargeTime_ / chargeMaxTime_, 0.0f, 1.0f);
}

// =============================================================
// Move
// =============================================================
void Player::Move(InputManager* input, float deltaTime) {
	auto* keyboard = input->GetKeyboard();

	float moveX = 0.0f;
	if (keyboard->PuhsKey(DIK_A)) { moveX -= 1.0f; }
	if (keyboard->PuhsKey(DIK_D)) { moveX += 1.0f; }

	// チャージ中は移動速度ダウン
	float currentSpeed = speed_;
	if (isCharging_) {
		currentSpeed *= chargeMoveSpeedMult_;
	}

	position_.x += moveX * currentSpeed * deltaTime;

	if (moveX > 0.0f) {
		facingDirection_ = 1;
		targetRotationY_ = 3.14f;
	} else if (moveX < 0.0f) {
		facingDirection_ = -1;
		targetRotationY_ = 0.0f;
	}

	float diff = targetRotationY_ - currentRotationY_;
	while (diff > 3.14159f) { diff -= 6.28318f; }
	while (diff < -3.14159f) { diff += 6.28318f; }
	currentRotationY_ += diff * rotationSpeed_ * deltaTime;
	rotation_.y = currentRotationY_;

	if (keyboard->TriggerKey(DIK_W) && jumpCount_ < maxJumpCount_) {
		velocityY_ = jumpPower_;
		isGrounded_ = false;
		jumpCount_++;
	}

	velocityY_ += gravity_ * deltaTime;
	position_.y += velocityY_ * deltaTime;

	if (position_.y <= groundY_) {
		position_.y = groundY_;
		velocityY_ = 0.0f;
		isGrounded_ = true;
		jumpCount_ = 0;
	}

	position_.x = std::clamp(position_.x, -15.0f, 15.0f);
}

// =============================================================
// ShootNormal（通常射撃 - 従来と同じ）
// =============================================================
void Player::ShootNormal(InputManager* input, float deltaTime) {
	fireTimer_ -= deltaTime;
	auto* keyboard = input->GetKeyboard();

	if (keyboard->PuhsKey(DIK_SPACE) && fireTimer_ <= 0.0f) {
		float dirX = static_cast<float>(facingDirection_);
		float dirY = 0.0f;

		if (!isGrounded_) {
			float randY = ((float)std::rand() / RAND_MAX - 0.5f) * 2.0f * airSpread_;
			dirY += randY;
			float len = std::sqrt(dirX * dirX + dirY * dirY);
			if (len > 0.0f) { dirX /= len; dirY /= len; }
		}

		auto bullet = std::make_unique<Bullet>();
		bullet->Initialize(
			object3DManager_, dxCore_, bulletType_,
			position_,
			Vector3{ dirX, dirY, 0.0f }
		);
		bullets_.push_back(std::move(bullet));

		fireTimer_ = fireRate_;
	}
}

// =============================================================
// ShootCharge（チャージショット）
// =============================================================
void Player::ShootCharge(InputManager* input, float deltaTime) {
	auto* keyboard = input->GetKeyboard();

	if (keyboard->PuhsKey(DIK_SPACE)) {
		// === チャージ中 ===
		isCharging_ = true;
		chargeTime_ += deltaTime;
		chargeTime_ = std::min(chargeTime_, chargeMaxTime_);

		// チャージ中パーティクル
		UpdateChargeParticles(deltaTime);

	} else if (isCharging_) {
		// === SPACEを離した → 発射 ===
		float t = GetChargeRatio();

		// チャージ量に応じたパラメータ補間
		float scale  = chargeMinScale_  + t * (chargeMaxScale_  - chargeMinScale_);
		float radius = chargeMinRadius_ + t * (chargeMaxRadius_ - chargeMinRadius_);
		int damage   = chargeMinDamage_ + static_cast<int>(t * (chargeMaxDamage_ - chargeMinDamage_));
		float bulletSpeed = chargeMinSpeed_ + t * (chargeMaxSpeed_ - chargeMinSpeed_);

		float dirX = static_cast<float>(facingDirection_);
		float dirY = 0.0f;

		// 空中ブレ（チャージ量が高いほどブレ減少）
		if (!isGrounded_) {
			float spreadMult = 1.0f - t * 0.7f; // 最大チャージで70%ブレ軽減
			float randY = ((float)std::rand() / RAND_MAX - 0.5f) * 2.0f * airSpread_ * spreadMult;
			dirY += randY;
			float len = std::sqrt(dirX * dirX + dirY * dirY);
			if (len > 0.0f) { dirX /= len; dirY /= len; }
		}

		auto bullet = std::make_unique<Bullet>();
		bullet->Initialize(
			object3DManager_, dxCore_, bulletType_,
			position_,
			Vector3{ dirX, dirY, 0.0f }
		);
		bullet->SetChargeParams(scale, radius, damage);
		bullet->SetSpeed(bulletSpeed);

		bullets_.push_back(std::move(bullet));

		// チャージリセット
		isCharging_ = false;
		chargeTime_ = 0.0f;
		chargeParticleTimer_ = 0.0f;
	}
}

// =============================================================
// UpdateChargeParticles（チャージ中の吸い込みパーティクル）
// =============================================================
void Player::UpdateChargeParticles(float deltaTime) {
	chargeParticleTimer_ += deltaTime;

	if (chargeParticleTimer_ >= chargeParticleRate_) {
		chargeParticleTimer_ = 0.0f;

		float t = GetChargeRatio();

		// チャージが進むほど多くのパーティクルを出す
		int count = 1 + static_cast<int>(t * 4.0f); // 1~5個

		// プレイヤー付近のランダムな位置からパーティクルを発生
		// ParticleManagerのEmitで位置を指定
		// パーティクルがプレイヤーに向かって集まる演出は
		// パーティクルの初速をプレイヤー方向に向ける形で実現

		// 発射口の位置（プレイヤーの前方）
		Vector3 chargeCenter = position_;
		chargeCenter.x += static_cast<float>(facingDirection_) * 1.5f;

		// 周囲のランダム位置から発生
		for (int i = 0; i < count; ++i) {
			float angle = ((float)std::rand() / RAND_MAX) * 6.28318f;
			float dist = 2.0f + ((float)std::rand() / RAND_MAX) * 3.0f; // 2~5の距離

			Vector3 spawnPos = chargeCenter;
			spawnPos.x += std::cos(angle) * dist;
			spawnPos.y += std::sin(angle) * dist;

			// パーティクルのサイズを小さめに設定
			ParticleManager::GetInstance()->SetParticleScale(0.15f + t * 0.1f);
			ParticleManager::GetInstance()->SetVelocityScale(3.0f + t * 2.0f);

			// 中心に向かう方向をセット
			Vector3 toCenter = {
				chargeCenter.x - spawnPos.x,
				chargeCenter.y - spawnPos.y,
				0.0f
			};
			ParticleManager::GetInstance()->SetVelocityDirection(toCenter);

			ParticleManager::GetInstance()->Emit("circle", spawnPos, 1);
		}

		// 設定をリセット（他の箇所に影響しないように）
		ParticleManager::GetInstance()->SetParticleScale(1.0f);
		ParticleManager::GetInstance()->SetVelocityScale(1.0f);
		ParticleManager::GetInstance()->ResetVelocityDirection();
	}
}

// =============================================================
// UpdateBullets
// =============================================================
void Player::UpdateBullets(float deltaTime) {
	for (auto& bullet : bullets_) {
		bullet->Update(deltaTime);
	}

	bullets_.erase(
		std::remove_if(bullets_.begin(), bullets_.end(),
			[](const std::unique_ptr<Bullet>& b) {
				return !b->IsActive();
			}
		),
		bullets_.end()
	);
}
