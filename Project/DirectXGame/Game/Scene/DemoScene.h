#pragma once
#include "GameScene.h"
#include "Vector3.h"
#include <memory>
#include <vector>
#include <string>
#include <random>

// 前方宣言
class DebugCamera;
class Object3DInstance;
class Skybox;
class Camera;
class PrimitiveMesh;
class AnimatedModelInstance;
class AnimatedObject3DInstance;
class GPUParticleManager;
class IImGuiEditable;
class SplineCurveActor;

class DemoScene :public GameScene
{
public:
	/// <summary>
	/// 乱数（HitEffect の向き）の種を決める
	/// </summary>
	DemoScene();

	/// <summary>
	/// デストラクタ
	/// </summary>
	~DemoScene() override;

	/// <summary>
	/// JSON のセルフテスト・カメラ・Skybox・アニメーションモデル・サウンドを用意し、demo.json があれば配置を読む
	/// </summary>
	void Initialize() override;

	/// <summary>
	/// 動的エンティティと HitEffect を捨てる
	/// </summary>
	void Finalize() override;

	/// <summary>
	/// 技術確認用の更新（DebugDraw・入力のログ・2D/3D サウンド・HitEffect・オブジェクトとエフェクトの更新）
	/// </summary>
	void Update() override;

	/// <summary>
	/// Skybox → 3D オブジェクト → パーティクル・エフェクト → デバッグ線 → スプライトの順に描く
	/// </summary>
	void Draw() override;

	/// <summary>シーンのカメラを取得</summary>
	/// <returns>シーンのカメラ</returns>
	Camera* GetCamera() override { return camera_.get(); }

#ifdef USE_IMGUI
	/// <summary>動的オブジェクト判定。GameScene の判定に加えてアニメーションモデル（sneakWalk）も対象にする</summary>
	/// <param name="editable">調べるエンティティ</param>
	/// <returns>シーンの動的オブジェクトなら true</returns>
	bool IsDynamicObject(IImGuiEditable* editable) const override;
#endif

protected:
	/// <summary>
	/// JSON の "scene" に書く名前（シーン保存/読込の本体は GameScene 側に集約済み。
	/// DemoScene は固有の除外・後処理が無いので、名前だけ返す）
	/// </summary>
	/// <returns>"DemoScene"</returns>
	std::string GetSceneJsonName() const override;

private:

	// DebugCamera は GameScene が管理（GetDebugCamera() で取得）

	// 動的エンティティ群（Object3D / Sprite / Animated / Spline / Primitive）は GameScene に集約済み

	// アニメーション付きモデル
	std::unique_ptr<AnimatedModelInstance> sneakWalk;
	std::unique_ptr<AnimatedObject3DInstance> sneakWalkInstance_;

	// 天球(Skybox)
	std::unique_ptr<Skybox> skybox_;

	// カメラ
	std::unique_ptr<Camera> camera_;

	// Primitiveメッシュ（動作確認用）
	std::unique_ptr<PrimitiveMesh> testPrimitive_;
	std::unique_ptr<PrimitiveMesh> testPrimitiveBox_;

	// Plane版HitEffect用の1枚分のデータ
	struct HitEffectPlane {
		std::unique_ptr<PrimitiveMesh> mesh;
		Vector3 initialScale;       // 初期スケール（縦長）
		float lifeTime = 0.6f;      // 寿命
		float currentTime = 0.0f;   // 現在の経過時間
	};

	// HitEffect（Plane版）の管理配列
	std::vector<HitEffectPlane> hitEffectPlanes_;

	// GPUParticleManager は Game が共通保持（Game::GetGPUParticleManager()）

	// HitEffect用のタイマー
	float hitEffectTimer_ = 0.0f;

	// ランダム生成用
	std::mt19937 randomEngine_;

	// 岩のボロノイ分割の確認（ボス必殺技で割る岩。中身は DemoScene.cpp）
	struct FracturePreview;
	std::unique_ptr<FracturePreview> fracturePreview_;

	// --- 3Dサウンド確認用 ---
	uint32_t sound3DHandle_ = 0;     // 静的3Dサウンドのハンドル
	uint32_t sound3DOrbitHandle_ = 0;     // 周回する3Dサウンドのハンドル
	float    sound3DOrbitTime_ = 0.0f;  // 周回タイマー
	bool     sound3DOrbitActive_ = false; // 周回中かどうか
};

