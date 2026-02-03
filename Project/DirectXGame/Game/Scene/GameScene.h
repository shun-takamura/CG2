#pragma once

#include "BaseScene.h"
#include <memory>
#include <vector>
#include <string>
#include "Player.h"
#include "Enemy.h"
#include "CollisionManager.h"

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

	// 当たり判定メソッド
	void CheckCollisions();

private:
	/// <summary>
	/// カメラシェイクを開始
	/// </summary>
	/// <param name="intensity">シェイクの強さ</param>
	/// <param name="duration">シェイクの持続時間</param>
	void StartCameraShake(float intensity, float duration);

	/// <summary>
	/// カメラシェイクの更新
	/// </summary>
	void UpdateCameraShake(float deltaTime);

	/// <summary>
	/// UIの描画
	/// </summary>
	void DrawUI();

	/// <summary>
	/// HPゲージの描画
	/// </summary>
	void DrawHPGauge(float x, float y, float width, float height, 
		int currentHP, int maxHP, bool isPlayer);

	/// <summary>
	/// 弾数ゲージの描画
	/// </summary>
	void DrawAmmoGauge(float x, float y);

	/// <summary>
	/// 操作説明の描画
	/// </summary>
	void DrawControlsUI();

private:

	// ===== スマブラ風カメラ =====
	float cameraMinZ_ = -12.0f;   // 最も寄った時のZ（近い）
	float cameraMaxZ_ = -30.0f;   // 最も引いた時のZ（遠い）
	float cameraDistMin_ = 3.0f;  // この距離以下なら最大ズーム
	float cameraDistMax_ = 25.0f; // この距離以上なら最大引き
	float cameraSmoothSpeed_ = 5.0f; // 追従の滑らかさ
	float cameraBaseY_ = 15.0f;     // 通常の高さ
	float cameraCloseUpY_ = 8.0f;   // 最大ズーム時の高さ（低めに寄る）

	// ===== カメラシェイク =====
	float cameraShakeTimer_ = 0.0f;
	float cameraShakeDuration_ = 0.0f;
	float cameraShakeIntensity_ = 0.0f;
	Vector3 cameraShakeOffset_{ 0.0f, 0.0f, 0.0f };

	ColliderAABB playerCollider_;
	ColliderAABB enemyCollider_;

	Vector3 currentCameraPos_{ 0.0f, 0.0f, -20.0f };
	Vector3 currentCameraTarget_{ 0.0f, 0.0f, 0.0f }; // Rotate計算に使う場合用

	// デバッグカメラ
	std::unique_ptr<DebugCamera> debugCamera_;

	// スプライト関連
	std::unique_ptr<SpriteInstance> cameraSprite_;
	std::unique_ptr<SpriteInstance> operation_;
	std::vector<std::unique_ptr<SpriteInstance>> sprites_;

	// ===== UI用スプライト =====

	// HPゲージ（プレイヤー）
	std::unique_ptr<SpriteInstance> hpGaugeBack_;
	std::unique_ptr<SpriteInstance> hpGaugeFill_;

	// HPゲージ（エネミー）
	std::unique_ptr<SpriteInstance> hpGaugeBackEnemy_;
	std::unique_ptr<SpriteInstance> hpGaugeFillEnemy_;

	std::unique_ptr<SpriteInstance> ammoGaugeBack_;       // 弾数ゲージ背景
	std::unique_ptr<SpriteInstance> ammoGaugeFill_;       // 弾数ゲージ
	std::unique_ptr<SpriteInstance> reloadGauge_;         // リロードゲージ

	// 3Dオブジェクト
	std::vector<std::unique_ptr<Object3DInstance>> object3DInstances_;

	// カメラ
	std::unique_ptr<Camera> camera_;

	// パーティクル用タイマー
	float emitTimer_ = 0.0f;

	std::unique_ptr<Player> player_;
	std::unique_ptr<Enemy> enemy_;
	std::unique_ptr<Object3DInstance> stage_;

	// ===== 天球 =====
	std::unique_ptr<Object3DInstance> skydome_;
};
