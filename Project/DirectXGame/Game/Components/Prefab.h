#pragma once

#include <string>

#include "EntityTag.h"
#include "SphereCollider.h"   // ColliderShape
#include "Vector2.h"
#include "Vector3.h"
#include "Vector4.h"
#include "PrimitiveGenerator.h" // RingParams / CylinderParams / HelixParams
#include "BillboardMode.h"
#include "TimeGroup.h"

/// <summary>
/// プリファブの種別
/// </summary>
enum class PrefabKind {
	Object3D,   // 静的モデル
	Animated,   // アニメーション付きモデル
	Primitive,  // 自前プリミティブ（Plane/Box/Sphere/Ring/Cylinder/Helix）
};

/// <summary>
/// Primitive プリファブ固有のパラメータ。
/// PrimitiveInstance / PrimitiveMesh の Inspector 編集項目をそのまま保存する。
/// </summary>
struct PrimitivePrefabParams {
	int primitiveType = 0; // PrimitiveInstance::PrimitiveType の int 値
	std::string texturePath;

	// マテリアル
	Vector4 color{ 1.0f, 1.0f, 1.0f, 1.0f };
	int   blendMode = 1;     // PrimitivePipeline::BlendMode (None=0, Normal=1, Add=2, ...)
	bool  depthWrite = true;
	float alphaReference = 0.0f;
	bool  cullBackface = false;
	int   samplerMode = 0;   // 0=WrapAll, 1=WrapU+ClampV, 2=ClampAll

	// UV
	bool    uvAutoScroll = false;
	Vector2 uvScrollSpeed{ 0.0f, 0.0f };
	Vector2 uvOffset{ 0.0f, 0.0f };
	Vector2 uvScale{ 1.0f, 1.0f };
	bool    uvFlipU = false;
	bool    uvFlipV = false;

	// ビルボード
	BillboardMode billboardMode = BillboardMode::None;

	// 時間グループ
	TimeGroup timeGroup = TimeGroup::World;

	// 形状パラメータ（種類に応じて使用するものが変わる）
	PrimitiveGenerator::RingParams     ringParams{};
	PrimitiveGenerator::CylinderParams cylinderParams{};
	PrimitiveGenerator::HelixParams    helixParams{};
};

/// <summary>
/// プリファブ定義。kind に応じて Object3D / Animated / Primitive を表現する。
/// Object3D / Animated は modelDir + modelFile を、Primitive は primitiveParams を使う。
/// </summary>
struct PrefabDef {
	std::string name;        // プリファブ名（ファイル名から取得）

	// ----- 種別 -----
	PrefabKind kind = PrefabKind::Object3D;
	// 旧フィールド：true=Animated、kind 導入後も後方互換のため残す
	bool isAnimated = false;

	// ----- 見た目（Object3D / Animated 用） -----
	std::string modelDir;    // 例: "Resources/Models/Drone"
	std::string modelFile;   // 例: "drone.mesh"

	// ----- Primitive 用 -----
	PrimitivePrefabParams primitiveParams;

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
