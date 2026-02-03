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
	/// <param name="bulletMode">"normal" or "charge"</param>
	void Initialize(
		Object3DManager* object3DManager,
		DirectXCore* dxCore,
		const std::string& modelName,
		const std::string& bulletType,
		const std::string& bulletMode
	);

	void Update(InputManager* input, float deltaTime);
	void Draw(DirectXCore* dxCore);
	void TakeDamage(int amount);

	/// <summary>
	/// ノックバックを適用
	/// </summary>
	/// <param name="direction">ノックバック方向（正規化済み）</param>
	/// <param name="force">ノックバックの強さ</param>
	void ApplyKnockback(const Vector3& direction, float force);

	// ===== ゲッター =====
	const Vector3& GetPosition() const { return position_; }
	int GetHP() const { return hp_; }
	int GetMaxHP() const { return maxHp_; }
	float GetDamageRatio() const;
	float GetDamageFlashTimer() const { return damageFlashTimer_; }
	bool GetIsAlive() const { return isAlive_; }

	const std::vector<std::unique_ptr<Bullet>>& GetBullets() const { return bullets_; }
	std::vector<std::unique_ptr<Bullet>>& GetBullets() { return bullets_; }

	/// <summary>
	/// チャージ中かどうか（外部からパーティクル等で参照）
	/// </summary>
	bool IsCharging() const { return isCharging_; }

	/// <summary>
	/// チャージ割合 0.0~1.0
	/// </summary>
	float GetChargeRatio() const;

	/// <summary>
	/// ダメージを受けた瞬間かどうか（カメラシェイク用）
	/// </summary>
	bool JustTookDamage() const { return justTookDamage_; }
	void ClearJustTookDamage() { justTookDamage_ = false; }

	// ===== 弾数・リロード関連ゲッター =====
	int GetCurrentAmmo() const { return currentAmmo_; }
	int GetMaxAmmo() const { return maxAmmo_; }
	bool IsReloading() const { return isReloading_; }
	float GetReloadProgress() const;

	// ===== 滑空関連ゲッター =====
	bool IsGliding() const { return isGliding_; }

private:
	void Move(InputManager* input, float deltaTime);
	void ShootNormal(InputManager* input, float deltaTime);
	void ShootCharge(InputManager* input, float deltaTime);
	void UpdateBullets(float deltaTime);
	void UpdateChargeParticles(float deltaTime);
	void UpdateKnockback(float deltaTime);
	void UpdateReload(InputManager* input, float deltaTime);

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

	// ===== 滑空 =====
	bool isGliding_ = false;
	float glideGravity_ = -5.0f;        // 滑空中の重力（通常より弱い）
	float glideMaxFallSpeed_ = -3.0f;   // 滑空中の最大落下速度

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
	std::string bulletMode_ = "normal";  // "normal" or "charge"
	float fireRate_ = 0.2f;
	float fireTimer_ = 0.0f;
	float airSpread_ = 0.4f;

	// ===== 弾数・リロード =====
	int currentAmmo_ = 10;       // 現在の弾数
	int maxAmmo_ = 10;           // 最大弾数
	bool isReloading_ = false;   // リロード中か
	float reloadTime_ = 1.5f;    // リロードにかかる時間
	float reloadTimer_ = 0.0f;   // 現在のリロード経過時間

	// ===== チャージショット =====
	bool isCharging_ = false;
	float chargeTime_ = 0.0f;           // 現在のチャージ秒数
	float chargeMaxTime_ = 2.0f;        // 最大チャージ秒数
	float chargeMinScale_ = 0.5f;       // 最小チャージ時のスケール
	float chargeMaxScale_ = 3.0f;       // 最大チャージ時のスケール
	float chargeMinRadius_ = 0.3f;      // 最小チャージ時の当たり判定
	float chargeMaxRadius_ = 1.5f;      // 最大チャージ時の当たり判定
	int chargeMinDamage_ = 10;          // 最小チャージ時のダメージ
	int chargeMaxDamage_ = 80;          // 最大チャージ時のダメージ
	float chargeMinSpeed_ = 30.0f;      // 最小チャージ時の弾速
	float chargeMaxSpeed_ = 15.0f;      // 最大チャージ時はやや遅く重厚感
	float chargeParticleTimer_ = 0.0f;  // パーティクル発生間隔用
	float chargeParticleRate_ = 0.05f;  // パーティクル発生間隔（秒）
	float chargeMoveSpeedMult_ = 0.5f;  // チャージ中の移動速度倍率
	int chargeAmmoCost_ = 3;            // チャージショットの弾消費

	// ===== ノックバック =====
	Vector3 knockbackVelocity_{ 0.0f, 0.0f, 0.0f };
	float knockbackDecay_ = 8.0f;       // ノックバックの減衰速度

	// ===== ダメージ演出 =====
	float damageFlashTimer_ = 0.0f;
	float damageFlashDuration_ = 0.3f;
	bool justTookDamage_ = false;       // ダメージを受けた瞬間フラグ（カメラシェイク用）
};
