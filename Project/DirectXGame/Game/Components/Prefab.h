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

	// ----- ScoreValue（敵プレハブ用：撃破時の獲得スコア） -----
	// Enemy/Boss タグでのみ使用。0 にすればこの敵は加点なし。
	int           scoreValue = 10;

	// ----- Bullet（弾プレハブ用：速度・寿命・ホーミング・貫通） -----
	bool          hasBullet = false;
	float         bulletSpeed          = 18.0f; // [units/sec]
	float         bulletLifetime       = 4.0f;  // [sec]
	float         bulletHomingStrength = 0.0f;  // [/sec] 軽ホーミング（最近敵）。0=直進
	float         bulletStrongHomingStrength = 0.0f; // [/sec] 強ホーミング（ロック中の敵）
	float         bulletColliderGrowth = 0.02f; // 進行 1m あたりの collider 半径拡大量
	bool          bulletPenetrate      = false; // 貫通フラグ
	float         bulletPenetrateDamageRate = 0.2f; // 貫通中の多段ヒット間隔 [sec]
	std::string   bulletPenetrateEffect;       // 貫通中ダメージ発生時のエフェクト名

	// ----- Melee（近接攻撃プレハブ用：発生/持続/後隙・オフセット・コンボ・本/持続あて） -----
	bool          hasMelee            = false;
	float         meleeStartup        = 0.05f;            // 入力から判定発生までの時間 [sec]
	float         meleeActiveDuration = 0.20f;            // 判定の持続時間 [sec]
	float         meleeRecovery       = 0.15f;            // 判定終了から次の行動までの後隙 [sec]
	Vector3       meleeOffset{ 0.0f, 0.0f, 2.0f };        // aim 基準オフセット 右(X)/上(Y)/前(Z)
	float         meleeComboWindow    = 0.40f;            // 後隙後の次段入力の受付猶予 [sec]
	float         meleeCleanWindow     = 0.08f;           // 本あて扱いの秒数
	float         meleeCleanMultiplier = 1.5f;            // 本あて倍率
	float         meleeLateMultiplier  = 0.7f;            // 持続あて倍率

	// ----- Carrier（運び屋プレハブ用：子敵の寿命・徘徊半径） -----
	bool          hasCarrier             = false;
	float         carrierChildLifetimeSec  = 10.0f;
	float         carrierChildWanderRadius = 8.0f;
	float         carrierChildMoveSpeed    = 5.0f;

	// ----- Charge（プレイヤープレハブ用：チャージ時間 + 連射間隔） -----
	bool          hasCharge        = false;
	float         chargeStage1Time = 3.0f;  // 1段階目完了までの秒数
	float         chargeStage2Time = 6.0f;  // 2段階目完了までの秒数（合計時間）
	float         chargeFireRate   = 0.12f; // 通常弾の連射間隔 [sec]

	// ----- Precision（プレイヤープレハブ用：精密射撃モード中の弾性能加算） -----
	bool          hasPrecision         = false;
	float         precisionSpeedAdd    = 0.0f; // [units/sec] 全弾の弾速に加算
	float         precisionHomingAdd   = 0.0f; // [/sec] 強ホーミング強度に加算（ロック弾のみ）

	// ----- エフェクトスロット -----
	// スロット名 → EffectManager 登録名（charge1 / charge2 / hit / death など）。
	// 空文字列はそのスロットに未割当を表す。Inspector の DnD で編集する。
	std::unordered_map<std::string, std::string> effects;

	// ----- 弾プレハブスロット（プレイヤープレハブ用） -----
	// スロット名（normal / charge1 / charge2）→ 弾プレハブ名。
	std::unordered_map<std::string, std::string> bulletPrefabs;
};
