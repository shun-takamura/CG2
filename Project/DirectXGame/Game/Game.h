#pragma once

#include "Framework.h"
#include <vector>
#include <string>

// 前方宣言
class DebugCamera;
class SpriteInstance;
class Object3DInstance;
class Camera;

/// <summary>
/// ゲーム（ゲーム固有の処理）
/// </summary>
class Game : public Framework {
public:
	/// <summary>
	/// コンストラクタ
	/// </summary>
	Game();

	/// <summary>
	/// デストラクタ
	/// </summary>
	~Game();

	/// <summary>
	/// 初期化
	/// </summary>
	void Initialize() override;

	/// <summary>
	/// 終了
	/// </summary>
	void Finalize() override;

	/// <summary>
	/// 毎フレーム更新
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
	SpriteInstance* sprite_ = nullptr;
	std::vector<SpriteInstance*> sprites_;

	// 3Dオブジェクト関連
	std::vector<Object3DInstance*> object3DInstances_;

	// カメラ
	Camera* camera_ = nullptr;

	// パーティクル用タイマー
	float emitTimer_ = 0.0f;
};