#pragma once

#include <string>

#include "EntityTag.h"
#include "SphereCollider.h"   // ColliderShape
#include "Vector3.h"

/// <summary>
/// プリファブ定義。モデル + タグ + コライダー（Sphere/OBB/Capsule） + デフォルトScale/Rotate。
/// isAnimated=true のときは AnimatedObject3DInstance 経由でインスタンス化する。
/// </summary>
struct PrefabDef {
	std::string name;        // プリファブ名（ファイル名から取得）

	// ----- 見た目 -----
	std::string modelDir;    // 例: "Resources/Models/Drone"
	std::string modelFile;   // 例: "drone.mesh"
	bool isAnimated = false; // true のとき AnimatedObject3DInstance を生成

	// ----- 分類 -----
	EntityTag tag = EntityTag::None;

	// ----- デフォルト Transform（配置時に適用、Translate は配置位置） -----
	Vector3 defaultScale{ 1.0f, 1.0f, 1.0f };
	Vector3 defaultRotate{ 0.0f, 0.0f, 0.0f };

	// ----- コライダー（オプション） -----
	bool          hasCollider = false;
	ColliderShape colliderShape = ColliderShape::Sphere;
	Vector3       colliderOffset{ 0.0f, 0.0f, 0.0f };
	// Sphere
	float         colliderRadius = 1.0f;
	// OBB
	Vector3       colliderHalfExtents{ 0.5f, 0.5f, 0.5f };
	// Capsule
	float         colliderCapsuleRadius = 0.5f;
	float         colliderCapsuleHeight = 1.0f;
};
