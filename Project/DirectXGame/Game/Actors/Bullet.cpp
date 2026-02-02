#include "Bullet.h"
#include "Object3DManager.h"
#include "Object3DInstance.h"
#include "DirectXCore.h"

// 弾ごとにユニークな名前を付けるためのカウンター
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
	// モデル生成
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

	// 速度 = 方向 × スピード
	velocity_.x = direction.x * speed_;
	velocity_.y = direction.y * speed_;
	velocity_.z = direction.z * speed_;

	model_->SetTranslate(position_);

	// TODO: 弾のスケールを小さくする
	// model_->SetScale({ 0.3f, 0.3f, 0.3f });

	lifeTimer_ = 0.0f;
	isActive_ = true;
}

void Bullet::Update(float deltaTime) {
	if (!isActive_) {
		return;
	}

	// 移動
	position_.x += velocity_.x * deltaTime;
	position_.y += velocity_.y * deltaTime;
	position_.z += velocity_.z * deltaTime;

	model_->SetTranslate(position_);
	model_->Update();

	// 生存時間チェック
	lifeTimer_ += deltaTime;
	if (lifeTimer_ >= lifeTime_) {
		isActive_ = false;
		return;
	}

	// 画面外チェック
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
	// TODO: ヒットエフェクト（パーティクル発生など）
}
