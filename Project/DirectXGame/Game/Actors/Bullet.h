#pragma once

#include <memory>
#include <string>
#include "Vector3.h"

// 前方宣言
class Object3DManager;
class Object3DInstance;
class DirectXCore;

/// <summary>
/// 弾クラス
/// </summary>
class Bullet {
public:
	Bullet();
	~Bullet();

	/// <summary>
	/// 初期化
	/// </summary>
	void Initialize(
		Object3DManager* object3DManager,
		DirectXCore* dxCore,
		const std::string& bulletModelName,
		const Vector3& startPos,
		const Vector3& direction
	);

	void Update(float deltaTime);
	void Draw(DirectXCore* dxCore);

	// ===== ゲッター =====
	const Vector3& GetPosition() const { return position_; }
	bool IsActive() const { return isActive_; }
	float GetRadius() const { return radius_; }
	int GetDamage() const { return damage_; }

	void OnHit();

	// ===== セッター（Initialize後、発射前に呼ぶ） =====

	/// <summary>
	/// チャージショット用パラメータ一括設定
	/// </summary>
	void SetChargeParams(float scale, float radius, int damage);

	/// <summary>
	/// 弾速を設定
	/// </summary>
	void SetSpeed(float speed);

private:
	std::unique_ptr<Object3DInstance> model_;

	Vector3 position_{ 0.0f, 0.0f, 0.0f };
	Vector3 velocity_{ 0.0f, 0.0f, 0.0f };

	float speed_ = 30.0f;
	float scale_ = 1.0f;
	float radius_ = 0.3f;
	int damage_ = 10;
	float lifeTime_ = 3.0f;
	float lifeTimer_ = 0.0f;
	bool isActive_ = true;

	float outOfBoundsX_ = 50.0f;
	float outOfBoundsY_ = 30.0f;
};
