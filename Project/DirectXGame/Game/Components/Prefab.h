#pragma once

#include <string>

#include "EntityTag.h"
#include "Vector3.h"

/// <summary>
/// プリファブ定義（MVP: モデル + タグ + コライダー + デフォルトScale/Rotate）。
/// シーンに配置すると、これを元に Object3DInstance を生成して各フィールドを反映する。
/// </summary>
struct PrefabDef {
	std::string name;        // プリファブ名（ファイル名から取得）

	// ----- 見た目 -----
	std::string modelDir;    // 例: "Resources/Models/Drone"
	std::string modelFile;   // 例: "drone.mesh"

	// ----- 分類 -----
	EntityTag tag = EntityTag::None;

	// ----- デフォルト Transform（配置時に適用、Translate は配置位置） -----
	Vector3 defaultScale{ 1.0f, 1.0f, 1.0f };
	Vector3 defaultRotate{ 0.0f, 0.0f, 0.0f };

	// ----- コライダー（オプション） -----
	bool   hasCollider = false;
	float  colliderRadius = 1.0f;
	Vector3 colliderOffset{ 0.0f, 0.0f, 0.0f };
};
