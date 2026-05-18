#include "CollisionManager.h"

#include "IImGuiEditable.h"
#include "SphereCollider.h"
#include "CollisionMatrix.h"
#include "EntityTag.h"
#include "Primitive/DebugDraw.h"
#include "MathUtility.h"

#include <algorithm>
#include <cmath>

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

namespace {
	struct WorldData {
		Vector3 center;
		Vector3 axes[3];  // 0=X, 1=Y, 2=Z（オーナーのオイラー回転に対応）
	};

	inline float Dot3(const Vector3& a, const Vector3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
	inline Vector3 SubV(const Vector3& a, const Vector3& b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
	inline Vector3 AddV(const Vector3& a, const Vector3& b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
	inline Vector3 MulS(const Vector3& a, float s) { return { a.x * s, a.y * s, a.z * s }; }

	// オーナーの translate + offset を中心、オイラー rotate から軸を取り出して入れる
	bool TryGetWorldData(IImGuiEditable* e, const Collider& c, WorldData& out) {
		Vector3* t = e->GetEditableTranslate();
		if (!t) return false;
		out.center = { t->x + c.offset.x, t->y + c.offset.y, t->z + c.offset.z };

		if (const Vector3* r = e->GetEditableRotate()) {
			Matrix4x4 rot = MakeRotateMatrix(*r);
			out.axes[0] = { rot.m[0][0], rot.m[0][1], rot.m[0][2] };
			out.axes[1] = { rot.m[1][0], rot.m[1][1], rot.m[1][2] };
			out.axes[2] = { rot.m[2][0], rot.m[2][1], rot.m[2][2] };
		} else {
			out.axes[0] = { 1.0f, 0.0f, 0.0f };
			out.axes[1] = { 0.0f, 1.0f, 0.0f };
			out.axes[2] = { 0.0f, 0.0f, 1.0f };
		}
		return true;
	}

	// 点 p に最も近い OBB 上の点
	Vector3 ClosestPointOnOBB(const Vector3& p, const Vector3& obbCenter,
		const Vector3 axes[3], const Vector3& halfExtents)
	{
		Vector3 d = SubV(p, obbCenter);
		Vector3 result = obbCenter;
		const float halfArr[3] = { halfExtents.x, halfExtents.y, halfExtents.z };
		for (int i = 0; i < 3; ++i) {
			float dist = Dot3(d, axes[i]);
			dist = std::clamp(dist, -halfArr[i], halfArr[i]);
			result = AddV(result, MulS(axes[i], dist));
		}
		return result;
	}

	Vector3 ClosestPointOnSegment(const Vector3& a, const Vector3& b, const Vector3& p) {
		Vector3 ab = SubV(b, a);
		float denom = Dot3(ab, ab);
		if (denom < 1e-6f) return a;
		float t = std::clamp(Dot3(SubV(p, a), ab) / denom, 0.0f, 1.0f);
		return AddV(a, MulS(ab, t));
	}

	// セグメント間最短距離（Christer Ericson）
	float SegmentSegmentDistSq(
		const Vector3& p1, const Vector3& q1,
		const Vector3& p2, const Vector3& q2)
	{
		Vector3 d1 = SubV(q1, p1);
		Vector3 d2 = SubV(q2, p2);
		Vector3 r  = SubV(p1, p2);
		float a = Dot3(d1, d1);
		float e = Dot3(d2, d2);
		float f = Dot3(d2, r);
		const float eps = 1e-6f;
		float s = 0.0f, t = 0.0f;

		if (a <= eps && e <= eps) {
			Vector3 dd = SubV(p1, p2);
			return Dot3(dd, dd);
		}
		if (a <= eps) {
			t = std::clamp(f / e, 0.0f, 1.0f);
		} else {
			float c = Dot3(d1, r);
			if (e <= eps) {
				s = std::clamp(-c / a, 0.0f, 1.0f);
				t = 0.0f;
			} else {
				float b = Dot3(d1, d2);
				float denom = a * e - b * b;
				s = (denom != 0.0f)
					? std::clamp((b * f - c * e) / denom, 0.0f, 1.0f)
					: 0.0f;
				t = (b * s + f) / e;
				if (t < 0.0f) { t = 0.0f; s = std::clamp(-c / a, 0.0f, 1.0f); }
				else if (t > 1.0f) { t = 1.0f; s = std::clamp((b - c) / a, 0.0f, 1.0f); }
			}
		}
		Vector3 c1 = AddV(p1, MulS(d1, s));
		Vector3 c2 = AddV(p2, MulS(d2, t));
		Vector3 dd = SubV(c1, c2);
		return Dot3(dd, dd);
	}

	bool TestSphereSphere(const Vector3& ca, float ra, const Vector3& cb, float rb) {
		Vector3 d = SubV(ca, cb);
		float sumR = ra + rb;
		return Dot3(d, d) <= sumR * sumR;
	}

	bool TestSphereOBB(const Vector3& sc, float sr,
		const Vector3& oc, const Vector3 axes[3], const Vector3& he)
	{
		Vector3 q = ClosestPointOnOBB(sc, oc, axes, he);
		Vector3 d = SubV(sc, q);
		return Dot3(d, d) <= sr * sr;
	}

	bool TestSphereCapsule(const Vector3& sc, float sr,
		const Vector3& cc, const Vector3 axes[3], float h, float cr)
	{
		float half = 0.5f * h;
		Vector3 a = AddV(cc, MulS(axes[1], +half));
		Vector3 b = AddV(cc, MulS(axes[1], -half));
		Vector3 q = ClosestPointOnSegment(a, b, sc);
		Vector3 d = SubV(sc, q);
		float sum = sr + cr;
		return Dot3(d, d) <= sum * sum;
	}

	bool TestCapsuleCapsule(
		const Vector3& cA, const Vector3 axA[3], float hA, float rA,
		const Vector3& cB, const Vector3 axB[3], float hB, float rB)
	{
		float halfA = 0.5f * hA;
		float halfB = 0.5f * hB;
		Vector3 a0 = AddV(cA, MulS(axA[1], +halfA));
		Vector3 a1 = AddV(cA, MulS(axA[1], -halfA));
		Vector3 b0 = AddV(cB, MulS(axB[1], +halfB));
		Vector3 b1 = AddV(cB, MulS(axB[1], -halfB));
		float distSq = SegmentSegmentDistSq(a0, a1, b0, b1);
		float sum = rA + rB;
		return distSq <= sum * sum;
	}

	// SAT による OBB-OBB（標準 15 軸テスト）
	bool TestOBBOBB(const Vector3& cA, const Vector3 axA[3], const Vector3& heA,
		const Vector3& cB, const Vector3 axB[3], const Vector3& heB)
	{
		const float eps = 1e-6f;
		float R[3][3];
		float AbsR[3][3];
		for (int i = 0; i < 3; ++i) {
			for (int j = 0; j < 3; ++j) {
				R[i][j] = Dot3(axA[i], axB[j]);
				AbsR[i][j] = std::fabs(R[i][j]) + eps;
			}
		}
		Vector3 tWorld = SubV(cB, cA);
		float t[3] = {
			Dot3(tWorld, axA[0]),
			Dot3(tWorld, axA[1]),
			Dot3(tWorld, axA[2])
		};
		const float aE[3] = { heA.x, heA.y, heA.z };
		const float bE[3] = { heB.x, heB.y, heB.z };

		// L = A axes
		for (int i = 0; i < 3; ++i) {
			float ra = aE[i];
			float rb = bE[0] * AbsR[i][0] + bE[1] * AbsR[i][1] + bE[2] * AbsR[i][2];
			if (std::fabs(t[i]) > ra + rb) return false;
		}
		// L = B axes
		for (int j = 0; j < 3; ++j) {
			float ra = aE[0] * AbsR[0][j] + aE[1] * AbsR[1][j] + aE[2] * AbsR[2][j];
			float rb = bE[j];
			if (std::fabs(t[0] * R[0][j] + t[1] * R[1][j] + t[2] * R[2][j]) > ra + rb) return false;
		}
		// L = A0 x B0..B2
		{ float ra = aE[1]*AbsR[2][0] + aE[2]*AbsR[1][0]; float rb = bE[1]*AbsR[0][2] + bE[2]*AbsR[0][1];
		  if (std::fabs(t[2]*R[1][0] - t[1]*R[2][0]) > ra+rb) return false; }
		{ float ra = aE[1]*AbsR[2][1] + aE[2]*AbsR[1][1]; float rb = bE[0]*AbsR[0][2] + bE[2]*AbsR[0][0];
		  if (std::fabs(t[2]*R[1][1] - t[1]*R[2][1]) > ra+rb) return false; }
		{ float ra = aE[1]*AbsR[2][2] + aE[2]*AbsR[1][2]; float rb = bE[0]*AbsR[0][1] + bE[1]*AbsR[0][0];
		  if (std::fabs(t[2]*R[1][2] - t[1]*R[2][2]) > ra+rb) return false; }
		// L = A1 x B0..B2
		{ float ra = aE[0]*AbsR[2][0] + aE[2]*AbsR[0][0]; float rb = bE[1]*AbsR[1][2] + bE[2]*AbsR[1][1];
		  if (std::fabs(t[0]*R[2][0] - t[2]*R[0][0]) > ra+rb) return false; }
		{ float ra = aE[0]*AbsR[2][1] + aE[2]*AbsR[0][1]; float rb = bE[0]*AbsR[1][2] + bE[2]*AbsR[1][0];
		  if (std::fabs(t[0]*R[2][1] - t[2]*R[0][1]) > ra+rb) return false; }
		{ float ra = aE[0]*AbsR[2][2] + aE[2]*AbsR[0][2]; float rb = bE[0]*AbsR[1][1] + bE[1]*AbsR[1][0];
		  if (std::fabs(t[0]*R[2][2] - t[2]*R[0][2]) > ra+rb) return false; }
		// L = A2 x B0..B2
		{ float ra = aE[0]*AbsR[1][0] + aE[1]*AbsR[0][0]; float rb = bE[1]*AbsR[2][2] + bE[2]*AbsR[2][1];
		  if (std::fabs(t[1]*R[0][0] - t[0]*R[1][0]) > ra+rb) return false; }
		{ float ra = aE[0]*AbsR[1][1] + aE[1]*AbsR[0][1]; float rb = bE[0]*AbsR[2][2] + bE[2]*AbsR[2][0];
		  if (std::fabs(t[1]*R[0][1] - t[0]*R[1][1]) > ra+rb) return false; }
		{ float ra = aE[0]*AbsR[1][2] + aE[1]*AbsR[0][2]; float rb = bE[0]*AbsR[2][1] + bE[1]*AbsR[2][0];
		  if (std::fabs(t[1]*R[0][2] - t[0]*R[1][2]) > ra+rb) return false; }
		return true;
	}

	// OBB-Capsule（近似）：線分を N サンプリングして Sphere-OBB 判定
	bool TestOBBCapsule(
		const Vector3& oc, const Vector3 oax[3], const Vector3& he,
		const Vector3& cc, const Vector3 cax[3], float h, float cr)
	{
		float half = 0.5f * h;
		Vector3 a = AddV(cc, MulS(cax[1], +half));
		Vector3 b = AddV(cc, MulS(cax[1], -half));
		constexpr int N = 6;
		for (int i = 0; i <= N; ++i) {
			float t = static_cast<float>(i) / static_cast<float>(N);
			Vector3 p = AddV(a, MulS(SubV(b, a), t));
			if (TestSphereOBB(p, cr, oc, oax, he)) return true;
		}
		return false;
	}

	bool TestPair(const Collider& ca, const WorldData& wA,
		const Collider& cb, const WorldData& wB)
	{
		switch (ca.shape) {
		case ColliderShape::Sphere:
			switch (cb.shape) {
			case ColliderShape::Sphere:  return TestSphereSphere(wA.center, ca.radius, wB.center, cb.radius);
			case ColliderShape::OBB:     return TestSphereOBB(wA.center, ca.radius, wB.center, wB.axes, cb.halfExtents);
			case ColliderShape::Capsule: return TestSphereCapsule(wA.center, ca.radius, wB.center, wB.axes, cb.capsuleHeight, cb.capsuleRadius);
			} break;
		case ColliderShape::OBB:
			switch (cb.shape) {
			case ColliderShape::Sphere:  return TestSphereOBB(wB.center, cb.radius, wA.center, wA.axes, ca.halfExtents);
			case ColliderShape::OBB:     return TestOBBOBB(wA.center, wA.axes, ca.halfExtents, wB.center, wB.axes, cb.halfExtents);
			case ColliderShape::Capsule: return TestOBBCapsule(wA.center, wA.axes, ca.halfExtents, wB.center, wB.axes, cb.capsuleHeight, cb.capsuleRadius);
			} break;
		case ColliderShape::Capsule:
			switch (cb.shape) {
			case ColliderShape::Sphere:  return TestSphereCapsule(wB.center, cb.radius, wA.center, wA.axes, ca.capsuleHeight, ca.capsuleRadius);
			case ColliderShape::OBB:     return TestOBBCapsule(wB.center, wB.axes, cb.halfExtents, wA.center, wA.axes, ca.capsuleHeight, ca.capsuleRadius);
			case ColliderShape::Capsule: return TestCapsuleCapsule(wA.center, wA.axes, ca.capsuleHeight, ca.capsuleRadius, wB.center, wB.axes, cb.capsuleHeight, cb.capsuleRadius);
			} break;
		}
		return false;
	}
}

void CollisionManager::Update() {
	for (IImGuiEditable* e : entities_) {
		if (e) e->GetCollider().isCollidingThisFrame = false;
	}

	const size_t n = entities_.size();
	for (size_t i = 0; i < n; ++i) {
		IImGuiEditable* a = entities_[i];
		if (!a) continue;
		Collider& ca = a->GetCollider();
		if (!ca.enabled) continue;
		EntityTag ta = a->GetTag();
		if (!CollisionMatrix::IsCollidableTag(ta)) continue;

		WorldData wA;
		if (!TryGetWorldData(a, ca, wA)) continue;

		for (size_t j = i + 1; j < n; ++j) {
			IImGuiEditable* b = entities_[j];
			if (!b) continue;
			Collider& cb = b->GetCollider();
			if (!cb.enabled) continue;
			EntityTag tb = b->GetTag();

			if (!CollisionMatrix::ShouldCollide(ta, tb)) continue;

			WorldData wB;
			if (!TryGetWorldData(b, cb, wB)) continue;

			if (TestPair(ca, wA, cb, wB)) {
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
	if (!drawDebugEnabled_) return;

	for (IImGuiEditable* e : entities_) {
		if (!e) continue;
		const Collider& c = e->GetCollider();
		if (!c.enabled || !c.showDebug) continue;
		WorldData w;
		if (!TryGetWorldData(e, c, w)) continue;

		Vector4 color;
		if (c.isCollidingThisFrame) {
			color = { 1.0f, 0.15f, 0.15f, 1.0f };
		} else {
			float r, g, b, a;
			GetTagColor(e->GetTag(), r, g, b, a);
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
