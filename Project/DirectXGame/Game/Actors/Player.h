#pragma once

#include <memory>
#include <vector>
#include <string>
#include "Vector3.h"

// 前方宣言
class Object3DManager;
class Object3DInstance;
class DirectXCore;
class InputManager;
class Bullet;

/// <summary>
/// プレイヤークラス
/// </summary>
class Player {
public:
	Player();
	~Player();

	/// <summary>
	/// 初期化
	/// </summary>
	/// <param name="object3DManager">3Dオブジェクトマネージャ</param>
	/// <param name="dxCore">DirectXコア</param>
	/// <param name="modelName">QRで選択されたモデルファイル名</param>
	/// <param name="bulletType">QRで選択された弾タイプ</param>
	void Initialize(
		Object3DManager* object3DManager,
		DirectXCore* dxCore,
		const std::string& modelName,
		const std::string& bulletType
	);

	/// <summary>
	/// 更新（毎フレーム呼ぶ）
	/// </summary>
	/// <param name="input">入力マネージャ</param>
	/// <param name="deltaTime">フレーム間の経過秒数</param>
	void Update(InputManager* input, float deltaTime);

	/// <summary>
	/// 描画
	/// </summary>
	/// <param name="dxCore">DirectXコア</param>
	void Draw(DirectXCore* dxCore);

	/// <summary>
	/// ダメージを受ける
	/// </summary>
	/// <param name="amount">ダメージ量</param>
	void TakeDamage(int amount);

	// ===== ゲッター =====

	/// <summary>
	/// 現在座標を取得（カメラ計算・当たり判定用）
	/// </summary>
	const struct Vector3& GetPosition() const { return position_; }

	int GetHP() const { return hp_; }
	int GetMaxHP() const { return maxHp_; }

	/// <summary>
	/// ダメージ割合（0.0 = 満タン, 1.0 = 瀕死）
	/// ヴィネット・グレースケール強度の算出に使う
	/// </summary>
	float GetDamageRatio() const;

	/// <summary>
	/// 被弾フラッシュの残り時間（0なら通常状態）
	/// </summary>
	float GetDamageFlashTimer() const { return damageFlashTimer_; }

	bool GetIsAlive() const { return isAlive_; }

	/// <summary>
	/// 弾リストへの参照（敵との当たり判定で使用）
	/// </summary>
	const std::vector<std::unique_ptr<Bullet>>& GetBullets() const { return bullets_; }
	std::vector<std::unique_ptr<Bullet>>& GetBullets() { return bullets_; }

private:
	// ===== 内部処理 =====

	/// <summary>
	/// 移動処理
	/// </summary>
	void Move(InputManager* input, float deltaTime);

	/// <summary>
	/// 射撃処理
	/// </summary>
	void Shoot(InputManager* input, float deltaTime);

	/// <summary>
	/// 弾の更新・削除
	/// </summary>
	void UpdateBullets(float deltaTime);

private:
	// ===== モデル =====
	std::unique_ptr<Object3DInstance> model_;
	Object3DManager* object3DManager_ = nullptr;
	DirectXCore* dxCore_ = nullptr;

	// ===== トランスフォーム =====
	Vector3 position_{ 0.0f, 0.0f, 0.0f };
	Vector3 rotation_{ 0.0f, 0.0f, 0.0f };
	float speed_ = 10.0f;
	float velocityY_ = 0.0f;
	float gravity_ = -30.0f;
	float jumpPower_ = 15.0f;
	float groundY_ = 0.0f;
	bool isGrounded_ = false;
	int jumpCount_ = 0;
	int maxJumpCount_ = 2;

	// ===== 向き =====
	float targetRotationY_ = 0.0f;
	float currentRotationY_ = 0.0f;
	float rotationSpeed_ = 10.0f;
	int facingDirection_ = 1;

	// ===== 戦闘 =====
	int hp_ = 100;
	int maxHp_ = 100;
	bool isAlive_ = true;

	// ===== 弾 =====
	std::vector<std::unique_ptr<Bullet>> bullets_;
	std::string bulletType_;
	float fireRate_ = 0.7f;   // 発射間隔（秒）
	float fireTimer_ = 0.0f;  // クールダウンタイマー
	float airSpread_ = 0.15f;

	// ===== ダメージ演出 =====
	float damageFlashTimer_ = 0.0f;
	float damageFlashDuration_ = 0.3f; // 被弾フラッシュの持続時間
};
