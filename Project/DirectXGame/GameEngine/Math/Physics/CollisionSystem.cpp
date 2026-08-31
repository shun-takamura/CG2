#include "CollisionSystem.h"

#include "CollisionGeometry.h"
#include "IImGuiEditable.h"
#include "Primitive/DebugDraw.h"
#include "Vector4.h"

#include <algorithm>
#include <unordered_map>

namespace {
	// エンティティポインタ → コライダー。
	// 破棄時に Unregister で除去するため dangling/ポインタ再利用のエイリアスは起きない。
	std::unordered_map<const IImGuiEditable*, Collider> g_colliders;
}

CollisionHostHooks CollisionSystem::hostHooks_{};

CollisionSystem* CollisionSystem::GetInstance() {
	static CollisionSystem instance;
	return &instance;
}

void CollisionSystem::SetHostHooks(const CollisionHostHooks& hooks) {
	hostHooks_ = hooks;
}

void CollisionSystem::Register(IImGuiEditable* e) {
	if (!e) return;
	if (std::find(entities_.begin(), entities_.end(), e) != entities_.end()) return;
	entities_.push_back(e);
}

void CollisionSystem::Unregister(IImGuiEditable* e) {
	auto it = std::find(entities_.begin(), entities_.end(), e);
	if (it != entities_.end()) {
		entities_.erase(it);
	}
	g_colliders.erase(e);
}

Collider& CollisionSystem::ColliderOf(IImGuiEditable* e) {
	return g_colliders[e];
}

bool CollisionSystem::Has(IImGuiEditable* e) const {
	return g_colliders.find(e) != g_colliders.end();
}

void CollisionSystem::Clear() {
	entities_.clear();
	g_colliders.clear();
}

void CollisionSystem::Update() {
	for (IImGuiEditable* e : entities_) {
		if (e) ColliderOf(e).isCollidingThisFrame = false;
	}

	const size_t n = entities_.size();
	for (size_t i = 0; i < n; ++i) {
		IImGuiEditable* a = entities_[i];
		if (!a) continue;
		Collider& ca = ColliderOf(a);
		if (!ca.enabled) continue;

		const int layerA = hostHooks_.getLayer ? hostHooks_.getLayer(a) : 0;

		CollisionGeometry::WorldData wA;
		if (!CollisionGeometry::TryGetWorldData(a, ca, wA)) continue;

		for (size_t j = i + 1; j < n; ++j) {
			IImGuiEditable* b = entities_[j];
			if (!b) continue;
			Collider& cb = ColliderOf(b);
			if (!cb.enabled) continue;

			const int layerB = hostHooks_.getLayer ? hostHooks_.getLayer(b) : 0;

			// ルール未配線なら全ペアを判定する
			if (hostHooks_.shouldCollide && !hostHooks_.shouldCollide(layerA, layerB)) continue;

			CollisionGeometry::WorldData wB;
			if (!CollisionGeometry::TryGetWorldData(b, cb, wB)) continue;

			if (CollisionGeometry::TestPair(ca, wA, cb, wB)) {
				ca.isCollidingThisFrame = true;
				cb.isCollidingThisFrame = true;

				// ゲーム側の結果処理（ダメージ交換など）
				if (hostHooks_.onHit) hostHooks_.onHit(a, b);

				if (ca.onCollision) ca.onCollision(b);
				if (cb.onCollision) cb.onCollision(a);
			}
		}
	}

#ifdef _DEBUG
	DrawDebug();
#endif
}

void CollisionSystem::DrawDebug() {
	if (!drawDebugEnabled_) return;

	for (IImGuiEditable* e : entities_) {
		if (!e) continue;
		const Collider& c = ColliderOf(e);
		if (!c.enabled || !c.showDebug) continue;
		CollisionGeometry::WorldData w;
		if (!CollisionGeometry::TryGetWorldData(e, c, w)) continue;

		Vector4 color;
		if (c.isCollidingThisFrame) {
			color = { 1.0f, 0.15f, 0.15f, 1.0f };
		} else if (hostHooks_.getLayerColor) {
			const int layer = hostHooks_.getLayer ? hostHooks_.getLayer(e) : 0;
			float r, g, b, a;
			hostHooks_.getLayerColor(layer, r, g, b, a);
			color = { r, g, b, 1.0f };
		} else {
			color = { 0.2f, 0.9f, 0.3f, 1.0f };
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
