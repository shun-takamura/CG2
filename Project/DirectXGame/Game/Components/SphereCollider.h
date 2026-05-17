#pragma once

#include <functional>

#include "Vector3.h"

class IImGuiEditable;

/// <summary>
/// 球コライダー（最小実装）。
/// Entity の translate + offset を中心、radius を半径とした球で当たり判定する。
/// </summary>
struct SphereCollider {
	/// 判定を行うか（タグが衝突可能でも、これが false なら無効）
	bool enabled = false;

	/// 半径
	float radius = 1.0f;

	/// オーナーの translate からのオフセット
	Vector3 offset{ 0.0f, 0.0f, 0.0f };

	/// デバッグ描画でコライダーを表示するか
	bool showDebug = true;

	/// 衝突発生時に呼ばれるコールバック（相手のエンティティを受け取る）
	std::function<void(IImGuiEditable* other)> onCollision;

	/// このフレームで衝突が発生したか（CollisionManager が毎フレ更新）。
	/// デバッグ描画で「赤くする」判定に使う。ゲームロジックからは IsPressed 的な使い方も可。
	bool isCollidingThisFrame = false;
};
