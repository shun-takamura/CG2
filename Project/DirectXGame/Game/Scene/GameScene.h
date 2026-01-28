#pragma once

#include "BaseScene.h"
#include <memory>
#include <vector>
#include <string>

// 前方宣言
class DebugCamera;
class SpriteInstance;
class Object3DInstance;
class Camera;

/// <summary>
/// ゲームシーン
/// </summary>
class GameScene : public BaseScene {
public:
	/// <summary>
	/// コンストラクタ
	/// </summary>
	GameScene();

	/// <summary>
	/// デストラクタ
	/// </summary>
	~GameScene() override;

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

	// カメラ
	std::unique_ptr<Camera> camera_;

	// パーティクル用タイマー
	float emitTimer_ = 0.0f;
};