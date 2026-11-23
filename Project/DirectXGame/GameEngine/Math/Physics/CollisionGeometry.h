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
	/// オーナーの translate + offset を中心、オイラー rotate（Rz·Ry·Rx）から軸を取り出してワールドの姿勢を埋める。
	/// rotate を持たないエンティティはワールド軸のまま
	/// </summary>
	/// <param name="e">コライダーの持ち主</param>
	/// <param name="c">コライダー</param>
	/// <param name="out">姿勢を書く先</param>
	/// <returns>埋めたら true（translate を持たないエンティティは false）</returns>
	bool TryGetWorldData(IImGuiEditable* e, const Collider& c, WorldData& out);

	/// <summary>
	/// 形状の組み合わせを解決して交差判定する（OBB と Capsule の組は近似）
	/// </summary>
	/// <param name="ca">A のコライダー</param>
	/// <param name="wA">A のワールドの姿勢</param>
	/// <param name="cb">B のコライダー</param>
	/// <param name="wB">B のワールドの姿勢</param>
	/// <returns>交差していれば true</returns>
	bool TestPair(const Collider& ca, const WorldData& wA,
		const Collider& cb, const WorldData& wB);

	//====================
	// 低レベル判定（独自のブロードフェーズを書く場合に直接使える）
	//====================
	/// <summary>球と球</summary>
	/// <param name="ca">A の中心</param>
	/// <param name="ra">A の半径</param>
	/// <param name="cb">B の中心</param>
	/// <param name="rb">B の半径</param>
	/// <returns>接触・交差していれば true</returns>
	bool TestSphereSphere(const Vector3& ca, float ra, const Vector3& cb, float rb);

	/// <summary>球と OBB（OBB 上の最近点との距離で判定）</summary>
	/// <param name="sc">球の中心</param>
	/// <param name="sr">球の半径</param>
	/// <param name="oc">OBB の中心</param>
	/// <param name="axes">OBB のローカル X/Y/Z 軸（単位ベクトル）</param>
	/// <param name="he">OBB の半幅</param>
	/// <returns>接触・交差していれば true</returns>
	bool TestSphereOBB(const Vector3& sc, float sr,
		const Vector3& oc, const Vector3 axes[3], const Vector3& he);

	/// <summary>球とカプセル（カプセルはローカル Y 軸沿い）</summary>
	/// <param name="sc">球の中心</param>
	/// <param name="sr">球の半径</param>
	/// <param name="cc">カプセルの中心</param>
	/// <param name="axes">カプセルのローカル X/Y/Z 軸</param>
	/// <param name="h">円柱部分の高さ（両端の半球は含まない）</param>
	/// <param name="cr">カプセルの半径</param>
	/// <returns>接触・交差していれば true</returns>
	bool TestSphereCapsule(const Vector3& sc, float sr,
		const Vector3& cc, const Vector3 axes[3], float h, float cr);

	/// <summary>カプセルとカプセル（芯の線分どうしの最短距離で判定）</summary>
	/// <param name="cA">A の中心</param>
	/// <param name="axA">A のローカル X/Y/Z 軸</param>
	/// <param name="hA">A の円柱部分の高さ</param>
	/// <param name="rA">A の半径</param>
	/// <param name="cB">B の中心</param>
	/// <param name="axB">B のローカル X/Y/Z 軸</param>
	/// <param name="hB">B の円柱部分の高さ</param>
	/// <param name="rB">B の半径</param>
	/// <returns>接触・交差していれば true</returns>
	bool TestCapsuleCapsule(
		const Vector3& cA, const Vector3 axA[3], float hA, float rA,
		const Vector3& cB, const Vector3 axB[3], float hB, float rB);

	/// <summary>SAT による OBB-OBB（標準 15 軸テスト）。</summary>
	/// <param name="cA">A の中心</param>
	/// <param name="axA">A のローカル X/Y/Z 軸（単位ベクトル）</param>
	/// <param name="heA">A の半幅</param>
	/// <param name="cB">B の中心</param>
	/// <param name="axB">B のローカル X/Y/Z 軸（単位ベクトル）</param>
	/// <param name="heB">B の半幅</param>
	/// <returns>交差していれば true（分離軸が 1 本でもあれば false）</returns>
	bool TestOBBOBB(const Vector3& cA, const Vector3 axA[3], const Vector3& heA,
		const Vector3& cB, const Vector3 axB[3], const Vector3& heB);

	/// <summary>OBB-Capsule（近似）：線分を N サンプリングして Sphere-OBB 判定。</summary>
	/// <param name="oc">OBB の中心</param>
	/// <param name="oax">OBB のローカル X/Y/Z 軸</param>
	/// <param name="he">OBB の半幅</param>
	/// <param name="cc">カプセルの中心</param>
	/// <param name="cax">カプセルのローカル X/Y/Z 軸</param>
	/// <param name="h">円柱部分の高さ</param>
	/// <param name="cr">カプセルの半径</param>
	/// <returns>どれかのサンプル球が OBB と交差していれば true</returns>
	bool TestOBBCapsule(
		const Vector3& oc, const Vector3 oax[3], const Vector3& he,
		const Vector3& cc, const Vector3 cax[3], float h, float cr);

	//====================
	// 補助
	//====================
	/// <summary>点 p に最も近い OBB 上の点。</summary>
	/// <param name="p">点</param>
	/// <param name="obbCenter">OBB の中心</param>
	/// <param name="axes">OBB のローカル X/Y/Z 軸（単位ベクトル）</param>
	/// <param name="halfExtents">OBB の半幅</param>
	/// <returns>最近点（p が中にあれば p）</returns>
	Vector3 ClosestPointOnOBB(const Vector3& p, const Vector3& obbCenter,
		const Vector3 axes[3], const Vector3& halfExtents);

	/// <summary>線分 ab 上で点 p に最も近い点。</summary>
	/// <param name="a">線分の始点</param>
	/// <param name="b">線分の終点</param>
	/// <param name="p">点</param>
	/// <returns>最近点（a と b がほぼ同じ点なら a）</returns>
	Vector3 ClosestPointOnSegment(const Vector3& a, const Vector3& b, const Vector3& p);

	/// <summary>線分同士の最短距離の二乗（Christer Ericson）。</summary>
	/// <param name="p1">線分 1 の始点</param>
	/// <param name="q1">線分 1 の終点</param>
	/// <param name="p2">線分 2 の始点</param>
	/// <param name="q2">線分 2 の終点</param>
	/// <returns>最短距離の二乗</returns>
	float SegmentSegmentDistSq(
		const Vector3& p1, const Vector3& q1,
		const Vector3& p2, const Vector3& q2);

} // namespace CollisionGeometry
