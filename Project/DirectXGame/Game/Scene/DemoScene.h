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

private:

	// デバッグカメラ
	std::unique_ptr<DebugCamera> debugCamera_;

	// スプライト関連
	std::unique_ptr<SpriteInstance> cameraSprite_;
	std::unique_ptr<SpriteInstance> sprite_;
	std::vector<std::unique_ptr<SpriteInstance>> sprites_;

	// 3Dオブジェクト
	std::vector<std::unique_ptr<Object3DInstance>> object3DInstances_;

	// アニメーション付きモデル
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

	// パーティクル用タイマー
	float emitTimer_ = 0.0f;

	// HitEffect用のタイマー
	float hitEffectTimer_ = 0.0f;

	// ランダム生成用
	std::mt19937 randomEngine_;
};

