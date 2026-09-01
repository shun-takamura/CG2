#pragma once

#include "Vector3.h"
#include "Collider.h"

class IImGuiEditable;

/// <summary>
/// コライダーの交差判定。状態を持たない純粋な幾何演算のみ。
/// 「誰と誰を判定するか」はここでは決めない（呼び出し側＝CollisionSystem やゲームのマネージャの責務）。
/// </summary>
namespace CollisionGeometry {

	/// <summary>
	/// ワールド空間に展開したコライダーの姿勢。
	/// </summary>
	struct WorldData {
		Vector3 center;
		Vector3 axes[3];  // 0=X, 1=Y, 2=Z（オーナーのオイラー回転に対応）
	};

	/// <summary>
	/// オーナーの translate + offset を中心、オイラー rotate から軸を取り出して埋める。
	/// translate を持たないエンティティは false を返す。
	/// </summary>
	bool TryGetWorldData(IImGuiEditable* e, const Collider& c, WorldData& out);

	/// <summary>
	/// 形状の組み合わせを解決して交差判定する。
	/// </summary>
	bool TestPair(const Collider& ca, const WorldData& wA,
		const Collider& cb, const WorldData& wB);

	//====================
	// 低レベル判定（独自のブロードフェーズを書く場合に直接使える）
	//====================
	bool TestSphereSphere(const Vector3& ca, float ra, const Vector3& cb, float rb);

	bool TestSphereOBB(const Vector3& sc, float sr,
		const Vector3& oc, const Vector3 axes[3], const Vector3& he);

	bool TestSphereCapsule(const Vector3& sc, float sr,
		const Vector3& cc, const Vector3 axes[3], float h, float cr);

	bool TestCapsuleCapsule(
		const Vector3& cA, const Vector3 axA[3], float hA, float rA,
		const Vector3& cB, const Vector3 axB[3], float hB, float rB);

	/// <summary>SAT による OBB-OBB（標準 15 軸テスト）。</summary>
	bool TestOBBOBB(const Vector3& cA, const Vector3 axA[3], const Vector3& heA,
		const Vector3& cB, const Vector3 axB[3], const Vector3& heB);

	/// <summary>OBB-Capsule（近似）：線分を N サンプリングして Sphere-OBB 判定。</summary>
	bool TestOBBCapsule(
		const Vector3& oc, const Vector3 oax[3], const Vector3& he,
		const Vector3& cc, const Vector3 cax[3], float h, float cr);

	//====================
	// 補助
	//====================
	/// <summary>点 p に最も近い OBB 上の点。</summary>
	Vector3 ClosestPointOnOBB(const Vector3& p, const Vector3& obbCenter,
		const Vector3 axes[3], const Vector3& halfExtents);

	/// <summary>線分 ab 上で点 p に最も近い点。</summary>
	Vector3 ClosestPointOnSegment(const Vector3& a, const Vector3& b, const Vector3& p);

	/// <summary>線分同士の最短距離の二乗（Christer Ericson）。</summary>
	float SegmentSegmentDistSq(
		const Vector3& p1, const Vector3& q1,
		const Vector3& p2, const Vector3& q2);

} // namespace CollisionGeometry
