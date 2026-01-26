#pragma once

#include "BaseScene.h"
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
	DebugCamera* debugCamera_ = nullptr;

	// スプライト関連
	SpriteInstance* cameraSprite = nullptr;
	SpriteInstance* sprite_ = nullptr;
	std::vector<SpriteInstance*> sprites_;

	// 3Dオブジェクト関連
	std::vector<Object3DInstance*> object3DInstances_;

	// カメラ
	Camera* camera_ = nullptr;

	// パーティクル用タイマー
	float emitTimer_ = 0.0f;
};