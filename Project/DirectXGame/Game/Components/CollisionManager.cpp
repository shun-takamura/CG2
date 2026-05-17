#include "CollisionManager.h"

#include "IImGuiEditable.h"
#include "SphereCollider.h"
#include "CollisionMatrix.h"
#include "EntityTag.h"
#include "Primitive/DebugDraw.h"

#include <algorithm>

CollisionManager* CollisionManager::GetInstance() {
	static CollisionManager instance;
	return &instance;
}

void CollisionManager::Register(IImGuiEditable* e) {
	if (!e) return;
	// 二重登録防止
	if (std::find(entities_.begin(), entities_.end(), e) != entities_.end()) return;
	entities_.push_back(e);
}

void CollisionManager::Unregister(IImGuiEditable* e) {
	auto it = std::find(entities_.begin(), entities_.end(), e);
	if (it != entities_.end()) {
		entities_.erase(it);
	}
}

namespace {
	// 球の中心位置を取得（entity の translate + collider.offset）。
	// 3Dエンティティ（GetEditableTranslate が非 null）でなければ nullopt 相当（false）。
	bool TryGetWorldCenter(IImGuiEditable* e, const SphereCollider& c, Vector3& out) {
		Vector3* t = e->GetEditableTranslate();
		if (!t) return false;
		out = { t->x + c.offset.x, t->y + c.offset.y, t->z + c.offset.z };
		return true;
	}
}

void CollisionManager::Update() {
	// このフレームの「衝突中」フラグをリセット
	for (IImGuiEditable* e : entities_) {
		if (e) e->GetCollider().isCollidingThisFrame = false;
	}

	// ペアごとに距離判定
	const size_t n = entities_.size();
	for (size_t i = 0; i < n; ++i) {
		IImGuiEditable* a = entities_[i];
		if (!a) continue;
		SphereCollider& ca = a->GetCollider();
		if (!ca.enabled) continue;
		EntityTag ta = a->GetTag();
		if (!CollisionMatrix::IsCollidableTag(ta)) continue;

		Vector3 centerA;
		if (!TryGetWorldCenter(a, ca, centerA)) continue;

		for (size_t j = i + 1; j < n; ++j) {
			IImGuiEditable* b = entities_[j];
			if (!b) continue;
			SphereCollider& cb = b->GetCollider();
			if (!cb.enabled) continue;
			EntityTag tb = b->GetTag();

			// タグペア許可マトリクスで早期 return
			if (!CollisionMatrix::ShouldCollide(ta, tb)) continue;

			Vector3 centerB;
			if (!TryGetWorldCenter(b, cb, centerB)) continue;

			// Sphere-Sphere: |centerA - centerB| < ra + rb
			float dx = centerA.x - centerB.x;
			float dy = centerA.y - centerB.y;
			float dz = centerA.z - centerB.z;
			float distSq = dx * dx + dy * dy + dz * dz;
			float sumR = ca.radius + cb.radius;
			if (distSq <= sumR * sumR) {
				ca.isCollidingThisFrame = true;
				cb.isCollidingThisFrame = true;
				if (ca.onCollision) ca.onCollision(b);
				if (cb.onCollision) cb.onCollision(a);
			}
		}
	}

#ifdef _DEBUG
	DrawDebug();
#endif
}

void CollisionManager::DrawDebug() {
	// グローバル OFF なら何も描かない
	if (!drawDebugEnabled_) return;

	for (IImGuiEditable* e : entities_) {
		if (!e) continue;
		const SphereCollider& c = e->GetCollider();
		if (!c.enabled || !c.showDebug) continue;
		Vector3 center;
		if (!TryGetWorldCenter(e, c, center)) continue;

		Vector4 color;
		if (c.isCollidingThisFrame) {
			// 衝突中は赤
			color = { 1.0f, 0.15f, 0.15f, 1.0f };
		} else {
			// 通常はタグ色
			float r, g, b, a;
			GetTagColor(e->GetTag(), r, g, b, a);
			color = { r, g, b, 1.0f };
		}
		DebugDraw::Sphere(center, c.radius, color, 16);
	}
}
