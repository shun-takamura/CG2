#pragma once

// 名前は歴史的に SphereCollider.h のままだが、現在は形状を選べる汎用コライダー（Collider）を定義する。
// （ファイル名のリネームは vcxproj/include の影響が広いため後回し）

#include <functional>

#include "Vector3.h"

class IImGuiEditable;

/// <summary>
/// コライダー形状。Inspector の Shape コンボで切り替える。
/// </summary>
enum class ColliderShape : int {
	Sphere  = 0,
	OBB     = 1,
	Capsule = 2,
};

/// <summary>
/// 形状を選べる汎用コライダー。
/// オーナーの translate + offset を中心、rotate（GetEditableRotate）を向きに使う。
/// </summary>
struct Collider {
	/// 判定を行うか（タグが衝突可能でも、これが false なら無効）
	bool enabled = false;

	/// デバッグ描画でコライダーを表示するか
	bool showDebug = true;

	/// 形状
	ColliderShape shape = ColliderShape::Sphere;

	/// オーナーの translate からのオフセット（ローカル空間。回転に追従させたい場合は OBB/Capsule の回転に乗る前提）
	Vector3 offset{ 0.0f, 0.0f, 0.0f };

	// ----- Sphere 用 -----
	float radius = 1.0f;

	// ----- OBB 用：ローカル X/Y/Z の半幅 -----
	Vector3 halfExtents{ 0.5f, 0.5f, 0.5f };

	// ----- Capsule 用：ローカル Y 軸沿い。height は円柱部分のみ（両端の半球は含まない） -----
	float capsuleRadius = 0.5f;
	float capsuleHeight = 1.0f;

	/// 衝突発生時に呼ばれるコールバック（相手のエンティティを受け取る）
	std::function<void(IImGuiEditable* other)> onCollision;

	/// このフレームで衝突が発生したか（CollisionManager が毎フレ更新）。
	/// デバッグ描画で「赤くする」判定に使う。ゲームロジックからは IsPressed 的な使い方も可。
	bool isCollidingThisFrame = false;
};

// 旧名互換（マクロ衝突や旧コード互換のため当面残す）
using SphereCollider = Collider;
