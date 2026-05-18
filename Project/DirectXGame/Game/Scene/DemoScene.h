#pragma once
#include "BaseScene.h"
#include "Vector3.h"
#include <memory>
#include <vector>
#include <string>
#include <random>

// 前方宣言
class DebugCamera;
class SpriteInstance;
class Object3DInstance;
class Skybox;
class Camera;
class PrimitiveMesh;
class AnimatedModelInstance;
class AnimatedObject3DInstance;
class GPUParticleManager;
class IImGuiEditable;
class SplineCurveActor;

class DemoScene :public BaseScene
{
public:
	/// <summary>
	/// コンストラクタ
	/// </summary>
	DemoScene();

	/// <summary>
	/// デストラクタ
	/// </summary>
	~DemoScene() override;

	/// <summary>
	/// 初期化
	/// </summary>
	void Initialize() override;

	/// <summary>
	/// 終了処理
	/// </summary>
	void Finalize() override;

	/// <summary>
	/// 更新
	/// </summary>
	void Update() override;

	/// <summary>
	/// 描画
	/// </summary>
	void Draw() override;

	Camera* GetCamera() override { return camera_.get(); }

	void AddDynamicObject(const std::string& dirPath, const std::string& filename,
		const Vector3& worldPos = {}) override;
	void RemoveDynamicObject(const std::string& name) override;
	void AddDynamicSprite(const std::string& texturePath, float clientX, float clientY) override;
	void RemoveDynamicSprite(const std::string& name) override;
	void AddDynamicAnimated(const std::string& dirPath, const std::string& filename,
		const Vector3& worldPos = {}) override;
	void RemoveDynamicAnimated(const std::string& name) override;
	void AddDynamicSpline(int tagInt, const Vector3& worldPos = {}) override;
	void RemoveDynamicSpline(const std::string& name) override;
	void InstantiatePrefab(const std::string& prefabName, const Vector3& worldPos = {}) override;
#ifdef USE_IMGUI
	bool IsDynamicObject(IImGuiEditable* editable) const override;
#endif

	// シーン保存/読込
	bool SaveSceneToJson(const std::string& filePath) override;
	bool LoadSceneFromJson(const std::string& filePath) override;

private:

	// DebugCamera は BaseScene が管理（GetDebugCamera() で取得）

	// スプライト関連
	std::unique_ptr<SpriteInstance> sprite_;
	std::vector<std::unique_ptr<SpriteInstance>> sprites_;

	// 3Dオブジェクト
	std::vector<std::unique_ptr<Object3DInstance>> object3DInstances_;

	// SceneEditorWindow からドロップで追加された動的エンティティ
	std::vector<std::unique_ptr<SpriteInstance>> dynamicSprites_;
	std::vector<std::unique_ptr<AnimatedModelInstance>> dynamicAnimatedModels_;
	std::vector<std::unique_ptr<AnimatedObject3DInstance>> dynamicAnimated_;
	std::vector<std::unique_ptr<SplineCurveActor>> dynamicSplines_;

	// アニメーション付きモデル
	std::unique_ptr<AnimatedModelInstance> sneakWalk;
	std::unique_ptr<AnimatedObject3DInstance> sneakWalkInstance_;

	std::unique_ptr<AnimatedModelInstance> animatedCubeModel_;
	std::unique_ptr<AnimatedObject3DInstance> animatedCubeInstance_;

	// 天球(Skybox)
	std::unique_ptr<Skybox> skybox_;

	// カメラ
	std::unique_ptr<Camera> camera_;

	// Primitiveメッシュ（動作確認用）
	std::unique_ptr<PrimitiveMesh> testPrimitive_;
	std::unique_ptr<PrimitiveMesh> testPrimitiveBox_;
	std::unique_ptr<PrimitiveMesh> testRing_;
	std::unique_ptr<PrimitiveMesh> testCylinder_;

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

	// パーティクル用タイマー
	float emitTimer_ = 0.0f;

	// HitEffect用のタイマー
	float hitEffectTimer_ = 0.0f;

	// ランダム生成用
	std::mt19937 randomEngine_;

	// --- 3Dサウンド確認用 ---
	uint32_t sound3DHandle_ = 0;     // 静的3Dサウンドのハンドル
	uint32_t sound3DOrbitHandle_ = 0;     // 周回する3Dサウンドのハンドル
	float    sound3DOrbitTime_ = 0.0f;  // 周回タイマー
	bool     sound3DOrbitActive_ = false; // 周回中かどうか
};

