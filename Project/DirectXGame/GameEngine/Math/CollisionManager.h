#pragma once

#include "Vector3.h"
#include <vector>
#include <functional>

// ===== コライダー形状 =====

/// <summary>
/// AABB（軸平行バウンディングボックス）
/// プレイヤーやエネミーのような四角形モデル用
/// </summary>
struct ColliderAABB {
	Vector3 center{ 0.0f, 0.0f, 0.0f };
	Vector3 halfSize{ 1.0f, 1.0f, 1.0f }; // 中心からの半サイズ

	Vector3 GetMin() const {
		return { center.x - halfSize.x, center.y - halfSize.y, center.z - halfSize.z };
	}
	Vector3 GetMax() const {
		return { center.x + halfSize.x, center.y + halfSize.y, center.z + halfSize.z };
	}
};

/// <summary>
/// 球コライダー
/// 弾のような球体モデル用
/// </summary>
struct ColliderSphere {
	Vector3 center{ 0.0f, 0.0f, 0.0f };
	float radius = 0.5f;
};

// ===== 判定結果 =====

/// <summary>
/// 衝突の組み合わせを識別するためのタグ
/// </summary>
enum class CollisionTag {
	Player,
	Enemy,
	PlayerBullet,
	EnemyBullet,
};

// ===== 判定関数群 =====

namespace CollisionHelper {

	/// <summary>
	/// AABB vs Sphere の当たり判定
	/// </summary>
	bool IsCollisionAABBSphere(const ColliderAABB& aabb, const ColliderSphere& sphere);

	/// <summary>
	/// Sphere vs Sphere の当たり判定
	/// </summary>
	bool IsCollisionSphereSphere(const ColliderSphere& a, const ColliderSphere& b);

	/// <summary>
	/// AABB vs AABB の当たり判定
	/// </summary>
	bool IsCollisionAABBAABB(const ColliderAABB& a, const ColliderAABB& b);

}
