#pragma once

#include <memory>
#include <vector>
#include <string>
#include"Vector3.h"

// 前方宣言
class Object3DManager;
class Object3DInstance;
class DirectXCore;
class Bullet;

/// <summary>
/// 敵クラス
/// </summary>
class Enemy {
public:
	Enemy();
	~Enemy();

	/// <summary>
	/// 初期化
	/// </summary>
	/// <param name="object3DManager">3Dオブジェクトマネージャ</param>
	/// <param name="dxCore">DirectXコア</param>
	/// <param name="modelName">モデルファイル名</param>
	/// <param name="bulletModelName">弾のモデルファイル名</param>
	/// <param name="spawnPos">初期座標</param>
	void Initialize(
		Object3DManager* object3DManager,
		DirectXCore* dxCore,
		const std::string& modelName,
		const std::string& bulletModelName,
		const struct Vector3& spawnPos
	);

	/// <summary>
	/// 更新（毎フレーム呼ぶ）
	/// </summary>
	/// <param name="playerPos">プレイヤーの現在座標</param>
	/// <param name="deltaTime">フレーム間秒数</param>
	void Update(const Vector3& playerPos, float deltaTime);

	/// <summary>
	/// 描画
	/// </summary>
	void Draw(DirectXCore* dxCore);

	/// <summary>
	/// ダメージを受ける
	/// </summary>
	void TakeDamage(int amount);

	// ===== ゲッター =====

	const Vector3& GetPosition() const { return position_; }
	int GetHP() const { return hp_; }
	int GetMaxHP() const { return maxHp_; }
	bool GetIsAlive() const { return isAlive_; }

	/// <summary>
	/// 当たり判定用の半径
	/// </summary>
	float GetCollisionRadius() const { return collisionRadius_; }

	/// <summary>
	/// 弾リストへの参照（プレイヤーとの当たり判定で使用）
	/// </summary>
	const std::vector<std::unique_ptr<Bullet>>& GetBullets() const { return bullets_; }
	std::vector<std::unique_ptr<Bullet>>& GetBullets() { return bullets_; }

private:
	// ===== AI内部処理 =====

	/// <summary>
	/// AI移動（プレイヤーに近づく / 距離を保つ）
	/// </summary>
	void MoveAI(const Vector3& playerPos, float deltaTime);

	/// <summary>
	/// プレイヤー方向を向く（Lerp回転）
	/// </summary>
	void UpdateRotation(const Vector3& playerPos, float deltaTime);

	/// <summary>
	/// AI射撃
	/// </summary>
	void ShootAI(const Vector3& playerPos, float deltaTime);

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
	float speed_ = 5.0f;          // プレイヤーより遅め
	float collisionRadius_ = 1.0f;

	// ===== 重力・ジャンプ =====
	float velocityY_ = 0.0f;
	float gravity_ = -30.0f;
	float groundY_ = 0.0f;
	bool isGrounded_ = false;

	// ===== 向き（Lerp回転）=====
	float targetRotationY_ = 0.0f;
	float currentRotationY_ = 0.0f;
	float rotationSpeed_ = 8.0f;   // プレイヤーよりやや遅め
	int facingDirection_ = -1;     // 初期は左向き（プレイヤー側）

	// ===== 戦闘 =====
	int hp_ = 100;
	int maxHp_ = 100;
	bool isAlive_ = true;

	// ===== 弾 =====
	std::vector<std::unique_ptr<Bullet>> bullets_;
	std::string bulletModelName_;
	float fireRate_ = 3.0f;        // プレイヤーより遅い発射間隔
	float fireTimer_ = 0.0f;

	// ===== AI行動パラメータ =====
	float preferredDistance_ = 8.0f;  // プレイヤーとの理想距離
	float distanceTolerance_ = 2.0f;  // この範囲内なら移動しない
	float jumpChance_ = 0.02f;        // 毎フレームのジャンプ確率
	float jumpPower_ = 12.0f;

	// ===== ダメージ演出 =====
	float damageFlashTimer_ = 0.0f;
	float damageFlashDuration_ = 0.3f;
};
