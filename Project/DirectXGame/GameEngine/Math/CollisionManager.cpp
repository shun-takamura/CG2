#include "CollisionManager.h"
#include <cmath>
#include <algorithm>

namespace CollisionHelper {

	// ===================================================================
	// AABB vs Sphere（課題のIsCollisionをベースに）
	// ===================================================================
	bool IsCollisionAABBSphere(const ColliderAABB& aabb, const ColliderSphere& sphere) {
		Vector3 aabbMin = aabb.GetMin();
		Vector3 aabbMax = aabb.GetMax();

		// 球の中心からAABBへの最近接点を求める
		Vector3 closestPoint{
			std::clamp(sphere.center.x, aabbMin.x, aabbMax.x),
			std::clamp(sphere.center.y, aabbMin.y, aabbMax.y),
			std::clamp(sphere.center.z, aabbMin.z, aabbMax.z),
		};

		// 最近接点と球の中心の距離
		float dx = closestPoint.x - sphere.center.x;
		float dy = closestPoint.y - sphere.center.y;
		float dz = closestPoint.z - sphere.center.z;
		float distSq = dx * dx + dy * dy + dz * dz;

		return distSq <= sphere.radius * sphere.radius;
	}

	// ===================================================================
	// Sphere vs Sphere
	// ===================================================================
	bool IsCollisionSphereSphere(const ColliderSphere& a, const ColliderSphere& b) {
		float dx = a.center.x - b.center.x;
		float dy = a.center.y - b.center.y;
		float dz = a.center.z - b.center.z;
		float distSq = dx * dx + dy * dy + dz * dz;

		float radiusSum = a.radius + b.radius;
		return distSq <= radiusSum * radiusSum;
	}

	// ===================================================================
	// AABB vs AABB
	// ===================================================================
	bool IsCollisionAABBAABB(const ColliderAABB& a, const ColliderAABB& b) {
		Vector3 aMin = a.GetMin();
		Vector3 aMax = a.GetMax();
		Vector3 bMin = b.GetMin();
		Vector3 bMax = b.GetMax();

		return (aMin.x <= bMax.x && aMax.x >= bMin.x) &&
			   (aMin.y <= bMax.y && aMax.y >= bMin.y) &&
			   (aMin.z <= bMax.z && aMax.z >= bMin.z);
	}

}
