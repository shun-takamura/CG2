#include "Bullet.h"
#include "Object3DManager.h"
#include "Object3DInstance.h"
#include "DirectXCore.h"
#include <cmath>

static uint32_t sBulletCount = 0;

Bullet::Bullet() {}

Bullet::~Bullet() {}

void Bullet::Initialize(
	Object3DManager* object3DManager,
	DirectXCore* dxCore,
	const std::string& bulletModelName,
	const Vector3& startPos,
	const Vector3& direction
) {
	model_ = std::make_unique<Object3DInstance>();
	std::string name = "Bullet_" + std::to_string(sBulletCount++);
	model_->Initialize(
		object3DManager,
		dxCore,
		"Resources",
		bulletModelName,
		name
	);

	position_ = startPos;

	velocity_.x = direction.x * speed_;
	velocity_.y = direction.y * speed_;
	velocity_.z = direction.z * speed_;

	model_->SetTranslate(position_);
	model_->SetScale({ scale_, scale_, scale_ });

	lifeTimer_ = 0.0f;
	isActive_ = true;
}

void Bullet::Update(float deltaTime) {
	if (!isActive_) {
		return;
	}

	position_.x += velocity_.x * deltaTime;
	position_.y += velocity_.y * deltaTime;
	position_.z += velocity_.z * deltaTime;

	model_->SetTranslate(position_);
	model_->Update();

	lifeTimer_ += deltaTime;
	if (lifeTimer_ >= lifeTime_) {
		isActive_ = false;
		return;
	}

	if (std::abs(position_.x) > outOfBoundsX_ ||
		std::abs(position_.y) > outOfBoundsY_) {
		isActive_ = false;
	}
}

void Bullet::Draw(DirectXCore* dxCore) {
	if (!isActive_) {
		return;
	}
	model_->Draw(dxCore);
}

void Bullet::OnHit() {
	isActive_ = false;
}

void Bullet::SetChargeParams(float scale, float radius, int damage) {
	scale_ = scale;
	radius_ = radius;
	damage_ = damage;

	// モデルのスケールも更新
	if (model_) {
		model_->SetScale({ scale_, scale_, scale_ });
	}
}

void Bullet::SetSpeed(float speed) {
	// 速度の方向を維持したまま速さだけ変える
	float currentSpeed = std::sqrt(
		velocity_.x * velocity_.x +
		velocity_.y * velocity_.y +
		velocity_.z * velocity_.z
	);

	if (currentSpeed > 0.0f) {
		float ratio = speed / currentSpeed;
		velocity_.x *= ratio;
		velocity_.y *= ratio;
		velocity_.z *= ratio;
	}

	speed_ = speed;
}
