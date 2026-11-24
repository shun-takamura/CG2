#pragma once
#include "Vector3.h"
#include <string>
#include <vector>

class IImGuiEditable;

/// <summary>
/// ビームが通る線分の上にいた敵 1 体分（IPlayerBeamHost::CollectBeamHits の結果）
/// </summary>
struct PlayerBeamHit {
	IImGuiEditable* target = nullptr;      // 当たった判定の持ち主（ボスの腕など）
	IImGuiEditable* damageRoot = nullptr;  // ダメージの持ち主（同じ持ち主には間隔ごとに 1 回）
	Vector3 point{ 0.0f, 0.0f, 0.0f };     // 線分の上で敵にいちばん近い所（ヒットエフェクトの位置）
};

/// <summary>
/// 自機のビーム（PlayerBeamPart）がシーンへ頼むことだけを並べた窓口（IFlightUnitHost と同じ流儀）。
/// </summary>
class IPlayerBeamHost {
public:
	virtual ~IPlayerBeamHost() = default;
	/// <summary>太さのある線分（カプセル）に重なっている敵（Enemy / Boss）を集める</summary>
	/// <param name="from">線分の始点（ワールド）</param>
	/// <param name="to">線分の終点（ワールド）</param>
	/// <param name="radius">線分の太さ（半径）[m]</param>
	/// <param name="out">当たった敵を追加する先</param>
	virtual void CollectBeamHits(const Vector3& from, const Vector3& to, float radius, std::vector<PlayerBeamHit>& out) = 0;
	/// <summary>ビームが敵に当たった：ダメージを入れ、ヒットエフェクトを出す</summary>
	/// <param name="hit">当たった敵</param>
	/// <param name="damage">入れるダメージ</param>
	/// <param name="hitEffect">当たった所に出すエフェクト名（空なら出さない）</param>
	/// <param name="firstHit">このビームがこの敵に初めて当たったか（音はこの時だけ鳴らす）</param>
	virtual void ApplyBeamHit(const PlayerBeamHit& hit, int damage, const std::string& hitEffect, bool firstHit) = 0;
};
