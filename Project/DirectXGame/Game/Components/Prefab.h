#pragma once

#include <string>
#include <unordered_map>

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

	// ----- HP（被ダメージ。プレイヤー/敵/ボス用） -----
	bool          hasHP = false;
	int           maxHP = 100;

	// ----- DamageDealer（与ダメージ。攻撃エンティティ用） -----
	bool          hasDamageDealer = false;
	int           damage = 10;             // 敵側用の固定ダメージ
	float         attackMultiplier = 1.0f; // プレイヤー攻撃プレハブ用の倍率

	// ----- AttackPower（プレイヤー用の基礎攻撃力） -----
	bool          hasAttackPower = false;
	int           attackPower = 10;

	// ----- Bullet（弾プレハブ用：速度・寿命・ホーミング） -----
	bool          hasBullet = false;
	float         bulletSpeed          = 18.0f; // [units/sec]
	float         bulletLifetime       = 4.0f;  // [sec]
	float         bulletHomingStrength = 0.0f;  // [/sec] 0=直進

	// ----- Carrier（運び屋プレハブ用：子敵の寿命・徘徊半径） -----
	bool          hasCarrier             = false;
	float         carrierChildLifetimeSec  = 10.0f;
	float         carrierChildWanderRadius = 8.0f;
	float         carrierChildMoveSpeed    = 5.0f;

	// ----- エフェクトスロット -----
	// スロット名 → EffectManager 登録名（charge1 / charge2 / hit / death など）。
	// 空文字列はそのスロットに未割当を表す。Inspector の DnD で編集する。
	std::unordered_map<std::string, std::string> effects;
};
