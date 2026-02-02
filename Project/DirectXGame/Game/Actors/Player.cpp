#include "Player.h"
#include "Object3DManager.h"
#include "Object3DInstance.h"
#include "DirectXCore.h"
#include "InputManager.h"
#include "KeyboardInput.h"
#include "Bullet.h"

Player::Player() {}

Player::~Player() {}

void Player::Initialize(
	Object3DManager* object3DManager,
	DirectXCore* dxCore,
	const std::string& modelName,
	const std::string& bulletType
) {
	object3DManager_ = object3DManager;
	dxCore_ = dxCore;
	bulletType_ = bulletType;

	// モデル生成（GameSceneと同じパターン）
	model_ = std::make_unique<Object3DInstance>();
	model_->Initialize(
		object3DManager_,
		dxCore_,
		"Resources",
		modelName,
		"Player"
	);

	// 初期位置
	position_ = { -5.0f, 0.0f, 0.0f };
	model_->SetTranslate(position_);
	model_->SetRotate({ 0.0f, 3.14f, 0.0f });

	// ステータス初期化
	hp_ = maxHp_;
	isAlive_ = true;
	fireTimer_ = 0.0f;
	damageFlashTimer_ = 0.0f;
}

// =============================================================
// Update
// =============================================================
void Player::Update(InputManager* input, float deltaTime) {
	if (!isAlive_) {
		return;
	}

	Move(input, deltaTime);
	Shoot(input, deltaTime);
	UpdateBullets(deltaTime);

	// 被弾フラッシュタイマーの減算
	if (damageFlashTimer_ > 0.0f) {
		damageFlashTimer_ -= deltaTime;
		if (damageFlashTimer_ < 0.0f) {
			damageFlashTimer_ = 0.0f;
		}
	}

	// モデルに座標を反映
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

	// プレイヤーモデル描画
	model_->Draw(dxCore);

	// 弾の描画
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

	// 被弾フラッシュ開始
	damageFlashTimer_ = damageFlashDuration_;

	if (hp_ <= 0) {
		hp_ = 0;
		isAlive_ = false;
		// TODO: 死亡演出（パーティクル等）
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
// Move（private）
// =============================================================
void Player::Move(InputManager* input, float deltaTime) {
	auto* keyboard = input->GetKeyboard();

	// --- X方向の移動 ---
	float moveX = 0.0f;
	if (keyboard->PuhsKey(DIK_A)) { moveX -= 1.0f; }
	if (keyboard->PuhsKey(DIK_D)) { moveX += 1.0f; }

	position_.x += moveX * speed_ * deltaTime;

	// --- 向きの更新（移動入力があるときだけ） ---
	if (moveX > 0.0f) {
		facingDirection_ = 1;
		targetRotationY_ = 3.14f;  // 右向き（モデルの初期向きに合わせて調整）
	} else if (moveX < 0.0f) {
		facingDirection_ = -1;
		targetRotationY_ = 0.0f;   // 左向き
	}

	// --- 回転のLerp補間 ---
	float diff = targetRotationY_ - currentRotationY_;
	// -π ~ π に収める（最短経路で回転）
	while (diff > 3.14159f) { diff -= 6.28318f; }
	while (diff < -3.14159f) { diff += 6.28318f; }
	currentRotationY_ += diff * rotationSpeed_ * deltaTime;
	rotation_.y = currentRotationY_;

	// --- ジャンプ（Wキー、接地中のみ） ---
	if (keyboard->TriggerKey(DIK_W) && jumpCount_ < maxJumpCount_) {
		velocityY_ = jumpPower_;
		isGrounded_ = false;
		jumpCount_++;
	}

	// --- 重力適用 ---
	velocityY_ += gravity_ * deltaTime;
	position_.y += velocityY_ * deltaTime;

	// --- 地面との衝突 ---
	if (position_.y <= groundY_) {
		position_.y = groundY_;
		velocityY_ = 0.0f;
		isGrounded_ = true;
		jumpCount_ = 0;  // ← 着地でリセット
	}

	// X方向の画面端クランプ
	position_.x = std::clamp(position_.x, -15.0f, 15.0f);
}

// =============================================================
// Shoot（private）
// =============================================================
void Player::Shoot(InputManager* input, float deltaTime) {
	// クールダウン減算
	fireTimer_ -= deltaTime;

	auto* keyboard = input->GetKeyboard();

	// スペースキーで射撃（あとでゲームパッドも追加可能）
	if (keyboard->PuhsKey(DIK_SPACE) && fireTimer_ <= 0.0f) {

		float dirX = static_cast<float>(facingDirection_);
		float dirY = 0.0f;

		// 空中なら弾にブレを追加
		if (!isGrounded_) {
			float randY = ((float)std::rand() / RAND_MAX - 0.5f) * 2.0f * airSpread_;
			dirY += randY;
			// 再正規化
			float len = std::sqrt(dirX * dirX + dirY * dirY);
			if (len > 0.0f) {
				dirX /= len;
				dirY /= len;
			}
		}

		auto bullet = std::make_unique<Bullet>();
		bullet->Initialize(
			object3DManager_,
			dxCore_,
			bulletType_,
			position_,
			Vector3{ dirX, dirY, 0.0f }
		);
		bullets_.push_back(std::move(bullet));

		fireTimer_ = fireRate_;
	}
}

// =============================================================
// UpdateBullets（private）
// =============================================================
void Player::UpdateBullets(float deltaTime) {
	// 全弾を更新
	for (auto& bullet : bullets_) {
		bullet->Update(deltaTime);
	}

	// 画面外 or 命中済みの弾を削除
	bullets_.erase(
		std::remove_if(bullets_.begin(), bullets_.end(),
			[](const std::unique_ptr<Bullet>& b) {
				return !b->IsActive();
			}
		),
		bullets_.end()
	);
}
