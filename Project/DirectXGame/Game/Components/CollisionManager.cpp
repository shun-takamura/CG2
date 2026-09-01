#include "CollisionManager.h"
#include "Components/Gameplay.h"

#include "IImGuiEditable.h"
#include "SphereCollider.h"
#include "CollisionMatrix.h"
#include "EntityTag.h"
#include "Primitive/DebugDraw.h"
#include "Physics/CollisionGeometry.h"  // 形状の交差判定はエンジン側に集約

#include <algorithm>

using CollisionGeometry::WorldData;

CollisionManager* CollisionManager::GetInstance() {
	static CollisionManager instance;
	return &instance;
}

void CollisionManager::Register(IImGuiEditable* e) {
	if (!e) return;
	if (std::find(entities_.begin(), entities_.end(), e) != entities_.end()) return;
	entities_.push_back(e);
}

void CollisionManager::Unregister(IImGuiEditable* e) {
	auto it = std::find(entities_.begin(), entities_.end(), e);
	if (it != entities_.end()) {
		entities_.erase(it);
	}
}

void CollisionManager::Update() {
	for (IImGuiEditable* e : entities_) {
		if (e) Gameplay::Of(e).GetCollider().isCollidingThisFrame = false;
	}

	const size_t n = entities_.size();
	for (size_t i = 0; i < n; ++i) {
		IImGuiEditable* a = entities_[i];
		if (!a) continue;
		Collider& ca = Gameplay::Of(a).GetCollider();
		if (!ca.enabled) continue;
		EntityTag ta = Gameplay::Of(a).GetTag();
		if (!CollisionMatrix::IsCollidableTag(ta)) continue;

		WorldData wA;
		if (!CollisionGeometry::TryGetWorldData(a, ca, wA)) continue;

		for (size_t j = i + 1; j < n; ++j) {
			IImGuiEditable* b = entities_[j];
			if (!b) continue;
			Collider& cb = Gameplay::Of(b).GetCollider();
			if (!cb.enabled) continue;
			EntityTag tb = Gameplay::Of(b).GetTag();

			if (!CollisionMatrix::ShouldCollide(ta, tb)) continue;

			WorldData wB;
			if (!CollisionGeometry::TryGetWorldData(b, cb, wB)) continue;

			if (CollisionGeometry::TestPair(ca, wA, cb, wB)) {
				ca.isCollidingThisFrame = true;
				cb.isCollidingThisFrame = true;

				// ----- 共通ダメージ交換 -----
				// CollisionMatrix::ShouldCollide で既にフレンドリーファイア等は除外されている前提。
				// DamageDealer 側の damage を HP 側に適用する。両方向に成立しうる（突進敵がプレイヤーに突っ込み、
				// 同時にプレイヤーが近接でカウンターしているケース等）。
				{
					DamageDealer& dda = Gameplay::Of(a).GetDamageDealer();
					HP&           hpb = Gameplay::Of(b).GetHP();
					if (dda.enabled && hpb.enabled) hpb.TakeDamage(dda.damage);

					DamageDealer& ddb = Gameplay::Of(b).GetDamageDealer();
					HP&           hpa = Gameplay::Of(a).GetHP();
					if (ddb.enabled && hpa.enabled) hpa.TakeDamage(ddb.damage);
				}

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
	if (!drawDebugEnabled_) return;

	for (IImGuiEditable* e : entities_) {
		if (!e) continue;
		const Collider& c = Gameplay::Of(e).GetCollider();
		if (!c.enabled || !c.showDebug) continue;
		WorldData w;
		if (!CollisionGeometry::TryGetWorldData(e, c, w)) continue;

		Vector4 color;
		if (c.isCollidingThisFrame) {
			color = { 1.0f, 0.15f, 0.15f, 1.0f };
		} else {
			float r, g, b, a;
			GetTagColor(Gameplay::Of(e).GetTag(), r, g, b, a);
			color = { r, g, b, 1.0f };
		}

		switch (c.shape) {
		case ColliderShape::Sphere:
			DebugDraw::Sphere(w.center, c.radius, color, 16);
			break;
		case ColliderShape::OBB:
			DebugDraw::OBB(w.center, w.axes, c.halfExtents, color);
			break;
		case ColliderShape::Capsule:
			DebugDraw::Capsule(w.center, w.axes, c.capsuleHeight, c.capsuleRadius, color, 16);
			break;
		}
	}
}
