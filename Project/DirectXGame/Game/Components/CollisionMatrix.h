#pragma once

#include "EntityTag.h"

/// <summary>
/// タグペアの当たり判定許可マトリクス。
/// Player と PlayerAttack など、明示的に「判定しない」ペアを早期 return するための表。
/// 値を変えるのは想定していないので関数ベースで定数判定する（コンパイル時最適化が効く）。
/// </summary>
namespace CollisionMatrix {

	/// <summary>
	/// そのタグが「コライダーを持つ可能性があるか」。Inspector の表示出し分け、
	/// CollisionManager のフィルタに使う。
	/// </summary>
	inline bool IsCollidableTag(EntityTag t) {
		switch (t) {
		case EntityTag::Player:
		case EntityTag::PlayerAttack:
		case EntityTag::Enemy:
		case EntityTag::EnemyAttack:
		case EntityTag::Boss:
		case EntityTag::Terrain:
			return true;
		default:
			return false;
		}
	}

	/// <summary>
	/// タグペアで衝突判定を行うべきか。順序非依存。
	/// </summary>
	inline bool ShouldCollide(EntityTag a, EntityTag b) {
		// 両方コライダーを持つ可能性が必要
		if (!IsCollidableTag(a) || !IsCollidableTag(b)) return false;

		// 順序非依存にするため、低いほうを first にする
		if (static_cast<int>(a) > static_cast<int>(b)) {
			EntityTag tmp = a; a = b; b = tmp;
		}

		// --- 明示的にスキップするペア ---
		// 自機 vs 自機弾（自分の弾は当たらない）
		if (a == EntityTag::Player && b == EntityTag::PlayerAttack) return false;
		// 敵 vs 敵弾、ボス vs 敵弾（敵は自分の弾に当たらない）
		if (a == EntityTag::Enemy && b == EntityTag::EnemyAttack) return false;
		if (a == EntityTag::EnemyAttack && b == EntityTag::Boss) return false;
		// 弾同士の相殺は今回はやらない
		if (a == EntityTag::PlayerAttack && b == EntityTag::EnemyAttack) return false;
		// 敵同士、ボス同士は無し
		if (a == EntityTag::Enemy && b == EntityTag::Enemy) return false;
		if (a == EntityTag::Enemy && b == EntityTag::Boss) return false;
		if (a == EntityTag::Boss && b == EntityTag::Boss) return false;
		// 地形同士は無し
		if (a == EntityTag::Terrain && b == EntityTag::Terrain) return false;

		// それ以外（Player vs Enemy, PlayerAttack vs Enemy, *vs Terrain など）は判定する
		return true;
	}

} // namespace CollisionMatrix
