#include "Enemy.h"
#include "Object3DManager.h"
#include "Object3DInstance.h"
#include "DirectXCore.h"
#include "Bullet.h"
#include <cmath>
#include <cstdlib>
#include <algorithm>

Enemy::Enemy() {}

Enemy::~Enemy() {}

void Enemy::Initialize(
	Object3DManager* object3DManager,
	DirectXCore* dxCore,
	const std::string& modelName,
	const std::string& bulletModelName,
	const Vector3& spawnPos
) {
	object3DManager_ = object3DManager;
	dxCore_ = dxCore;
	bulletModelName_ = bulletModelName;

	// モデル生成
	model_ = std::make_unique<Object3DInstance>();
	model_->Initialize(
		object3DManager_,
		dxCore_,
		"Resources",
		modelName,
		"Enemy"
	);

	// 初期位置
	position_ = spawnPos;
	model_->SetTranslate(position_);
	model_->SetRotate({ 0.0f, 0.0f, 0.0f });

	// 初期向き（左向き = プレイヤー側を想定）
	currentRotationY_ = 0.0f;
	targetRotationY_ = 0.0f;

	// ステータス初期化
	hp_ = maxHp_;
	isAlive_ = true;
	fireTimer_ = 0.0f;
	damageFlashTimer_ = 0.0f;
}

// =============================================================
// Update
// =============================================================
void Enemy::Update(const Vector3& playerPos, float deltaTime) {
	if (!isAlive_) {
		return;
	}

	// ノックバック更新
	UpdateKnockback(deltaTime);

	UpdateRotation(playerPos, deltaTime);
	MoveAI(playerPos, deltaTime);
	ShootAI(playerPos, deltaTime);
	UpdateBullets(deltaTime);

	// 被弾フラッシュタイマー
	if (damageFlashTimer_ > 0.0f) {
		damageFlashTimer_ -= deltaTime;
		if (damageFlashTimer_ < 0.0f) {
			damageFlashTimer_ = 0.0f;
		}
	}

	// モデルに反映
	model_->SetTranslate(position_);
	model_->SetRotate(rotation_);
	model_->Update();
}

// =============================================================
// Draw
// =============================================================
void Enemy::Draw(DirectXCore* dxCore) {
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
void Enemy::TakeDamage(int amount) {
	if (!isAlive_) {
		return;
	}

	hp_ -= amount;
	damageFlashTimer_ = damageFlashDuration_;

	if (hp_ <= 0) {
		hp_ = 0;
		isAlive_ = false;
		// TODO: 死亡演出（パーティクル等）
	}
}

// =============================================================
// ApplyKnockback
// =============================================================
void Enemy::ApplyKnockback(const Vector3& direction, float force) {
	knockbackVelocity_.x += direction.x * force;
	knockbackVelocity_.y += direction.y * force * 0.5f;  // Y方向は控えめに
	knockbackVelocity_.z += direction.z * force;
}

// =============================================================
// UpdateKnockback
// =============================================================
void Enemy::UpdateKnockback(float deltaTime) {
	// ノックバック速度を位置に適用
	position_.x += knockbackVelocity_.x * deltaTime;
	position_.y += knockbackVelocity_.y * deltaTime;

	// ノックバックを減衰
	float decay = std::exp(-knockbackDecay_ * deltaTime);
	knockbackVelocity_.x *= decay;
	knockbackVelocity_.y *= decay;
	knockbackVelocity_.z *= decay;

	// 十分小さくなったらゼロに
	if (std::abs(knockbackVelocity_.x) < 0.01f) knockbackVelocity_.x = 0.0f;
	if (std::abs(knockbackVelocity_.y) < 0.01f) knockbackVelocity_.y = 0.0f;
}

// =============================================================
// UpdateRotation（private）- プレイヤー方向を向く
// =============================================================
void Enemy::UpdateRotation(const Vector3& playerPos, float deltaTime) {
	// プレイヤーが自分より左か右か
	if (playerPos.x < position_.x) {
		facingDirection_ = -1;
		targetRotationY_ = 0.0f;     // 左向き
	}
	else {
		facingDirection_ = 1;
		targetRotationY_ = 3.14159f; // 右向き
	}

	// Lerp補間（最短経路で回転）
	float diff = targetRotationY_ - currentRotationY_;
	while (diff > 3.14159f) { diff -= 6.28318f; }
	while (diff < -3.14159f) { diff += 6.28318f; }
	currentRotationY_ += diff * rotationSpeed_ * deltaTime;

	rotation_.y = currentRotationY_;
}

// =============================================================
// MoveAI（private）- プレイヤーとの距離を保つ
// =============================================================
void Enemy::MoveAI(const Vector3& playerPos, float deltaTime) {

	// --- X方向: プレイヤーとの距離を preferredDistance_ に保つ ---
	float distX = playerPos.x - position_.x;
	float absDist = std::abs(distX);

	if (absDist > preferredDistance_ + distanceTolerance_) {
		// 遠すぎる → 近づく
		float dir = (distX > 0.0f) ? 1.0f : -1.0f;
		position_.x += dir * speed_ * deltaTime;
	}
	else if (absDist < preferredDistance_ - distanceTolerance_) {
		// 近すぎる → 離れる
		float dir = (distX > 0.0f) ? -1.0f : 1.0f;
		position_.x += dir * speed_ * deltaTime;
	}

	// --- ランダムジャンプ（接地中のみ） ---
	if (isGrounded_) {
		float roll = static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);
		if (roll < jumpChance_) {
			velocityY_ = jumpPower_;
			isGrounded_ = false;
		}
	}

	// --- 重力 ---
	velocityY_ += gravity_ * deltaTime;
	position_.y += velocityY_ * deltaTime;

	// --- 地面との衝突 ---
	if (position_.y <= groundY_) {
		position_.y = groundY_;
		velocityY_ = 0.0f;
		isGrounded_ = true;
	}
	position_.x = std::clamp(position_.x, -15.0f, 15.0f);
}

// =============================================================
// ShootAI（private）- 一定間隔でプレイヤー方向に射撃
// =============================================================
void Enemy::ShootAI(const Vector3& playerPos, float deltaTime) {
	fireTimer_ -= deltaTime;

	if (fireTimer_ <= 0.0f) {

		float dirX = playerPos.x - position_.x;
		float dirY = playerPos.y - position_.y;
		float length = std::sqrt(dirX * dirX + dirY * dirY);

		if (length < 0.01f) {
			return;
		}

		dirX /= length;
		dirY /= length;

		// ===== エイムのブレを追加 =====
		// -spread ~ +spread のランダムなズレ（大きいほどガバガバ）
		float spread = 0.1f; // ← ここで調整（0.0 = 完璧、0.5 = かなりガバ）
		float randX = ((float)std::rand() / RAND_MAX - 0.5f) * 2.0f * spread;
		float randY = ((float)std::rand() / RAND_MAX - 0.5f) * 2.0f * spread;
		dirX += randX;
		dirY += randY;

		// ブレ込みで再正規化
		float newLen = std::sqrt(dirX * dirX + dirY * dirY);
		if (newLen > 0.0f) {
			dirX /= newLen;
			dirY /= newLen;
		}

		auto bullet = std::make_unique<Bullet>();
		bullet->Initialize(
			object3DManager_,
			dxCore_,
			bulletModelName_,
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
void Enemy::UpdateBullets(float deltaTime) {
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
