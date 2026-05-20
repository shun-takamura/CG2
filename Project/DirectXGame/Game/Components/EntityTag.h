#pragma once

#include <string_view>

/// <summary>
/// シーン上のエンティティを分類するタグ。
/// 用途：当たり判定の有効/無効マトリクス、Inspector の出し分け、
///       Hierarchy のグルーピング、シーンJSONのフィルタなど。
/// </summary>
enum class EntityTag : int {
	None = 0,
	Player,            // 自機
	PlayerAttack,      // 自機の攻撃（弾・近接・必殺技のヒット判定等）
	Enemy,             // 敵全般
	EnemyAttack,       // 敵の攻撃（弾・突進・ボスの攻撃判定等）
	Boss,              // ボス（AI/HPバー扱いを Enemy と分離したい）
	Terrain,           // 地形・建物
	Skybox,            // 天球
	RailControlPoint,  // スプライン制御点（内部用、廃止予定。Spline系タグに置換中）
	SpawnPoint,        // 敵スポーン位置マーカー
	CameraPoint,       // カメラパスのキーフレーム
	FX,                // エフェクト発生源（パーティクル等）

	// ----- スプライン経路（役割ごとに分けて混在を防ぐ）-----
	PlayerRailSpline,    // プレイヤー機（およびレールカメラ）の走行レール
	EnemyPathSpline,     // 敵の移動経路
	FloatingPathSpline,  // 装飾オブジェクトの浮遊経路
	CameraPathSpline,    // カメラだけの独立経路（レールと分離したい時）
	CameraLookAtSpline,  // カメラの注視点用経路。CameraPathSpline と同じ t を共有して target を決める

	Count
};

inline std::string_view GetTagName(EntityTag t) {
	switch (t) {
	case EntityTag::None:             return "None";
	case EntityTag::Player:           return "Player";
	case EntityTag::PlayerAttack:     return "PlayerAttack";
	case EntityTag::Enemy:            return "Enemy";
	case EntityTag::EnemyAttack:      return "EnemyAttack";
	case EntityTag::Boss:             return "Boss";
	case EntityTag::Terrain:          return "Terrain";
	case EntityTag::Skybox:           return "Skybox";
	case EntityTag::RailControlPoint: return "RailControlPoint";
	case EntityTag::SpawnPoint:       return "SpawnPoint";
	case EntityTag::CameraPoint:      return "CameraPoint";
	case EntityTag::FX:               return "FX";
	case EntityTag::PlayerRailSpline:   return "PlayerRailSpline";
	case EntityTag::EnemyPathSpline:    return "EnemyPathSpline";
	case EntityTag::FloatingPathSpline: return "FloatingPathSpline";
	case EntityTag::CameraPathSpline:   return "CameraPathSpline";
	case EntityTag::CameraLookAtSpline: return "CameraLookAtSpline";
	default:                          return "";
	}
}

inline EntityTag TagFromName(std::string_view name) {
	for (int i = 0; i < static_cast<int>(EntityTag::Count); ++i) {
		auto t = static_cast<EntityTag>(i);
		if (GetTagName(t) == name) return t;
	}
	return EntityTag::None;
}

/// <summary>
/// Hierarchy のグループ見出しなどに使う色 (RGBA 0-1)
/// </summary>
inline void GetTagColor(EntityTag t, float& r, float& g, float& b, float& a) {
	a = 1.0f;
	switch (t) {
	case EntityTag::Player:           r = 0.40f; g = 0.85f; b = 1.00f; break; // 水色
	case EntityTag::PlayerAttack:     r = 0.55f; g = 0.85f; b = 1.00f; break;
	case EntityTag::Enemy:            r = 1.00f; g = 0.55f; b = 0.45f; break; // 赤
	case EntityTag::EnemyAttack:      r = 1.00f; g = 0.70f; b = 0.55f; break;
	case EntityTag::Boss:             r = 1.00f; g = 0.30f; b = 0.30f; break; // 濃い赤
	case EntityTag::Terrain:          r = 0.70f; g = 0.65f; b = 0.45f; break; // 茶
	case EntityTag::Skybox:           r = 0.55f; g = 0.55f; b = 0.85f; break;
	case EntityTag::RailControlPoint: r = 1.00f; g = 0.90f; b = 0.40f; break; // 黄
	case EntityTag::SpawnPoint:       r = 0.90f; g = 0.50f; b = 0.90f; break; // 紫
	case EntityTag::CameraPoint:      r = 0.60f; g = 1.00f; b = 0.60f; break; // 緑
	case EntityTag::FX:               r = 1.00f; g = 0.80f; b = 1.00f; break;
	case EntityTag::PlayerRailSpline:   r = 0.30f; g = 0.85f; b = 1.00f; break; // 水色
	case EntityTag::EnemyPathSpline:    r = 1.00f; g = 0.45f; b = 0.45f; break; // 赤
	case EntityTag::FloatingPathSpline: r = 0.70f; g = 1.00f; b = 0.70f; break; // 黄緑
	case EntityTag::CameraPathSpline:   r = 1.00f; g = 1.00f; b = 0.40f; break; // 黄
	case EntityTag::CameraLookAtSpline: r = 1.00f; g = 0.55f; b = 0.10f; break; // 橙（カメラパスと並走する注視点用）
	case EntityTag::None:
	default:                          r = 0.75f; g = 0.75f; b = 0.75f; break; // 灰
	}
}
