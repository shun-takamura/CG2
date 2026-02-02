#pragma once

#include <memory>
#include <string>
#include "Vector3.h"

// 前方宣言
class Object3DManager;
class Object3DInstance;
class DirectXCore;

/// <summary>
/// 弾クラス
/// </summary>
class Bullet {
public:
	Bullet();
	~Bullet();

	/// <summary>
	/// 初期化
	/// </summary>
	/// <param name="object3DManager">3Dオブジェクトマネージャ</param>
	/// <param name="dxCore">DirectXコア</param>
	/// <param name="bulletModelName">弾のモデルファイル名（QRで変更可能）</param>
	/// <param name="startPos">発射位置</param>
	/// <param name="direction">発射方向（正規化済み）</param>
	void Initialize(
		Object3DManager* object3DManager,
		DirectXCore* dxCore,
		const std::string& bulletModelName,
		const struct Vector3& startPos,
		const Vector3& direction
	);

	/// <summary>
	/// 更新
	/// </summary>
	/// <param name="deltaTime">フレーム間秒数</param>
	void Update(float deltaTime);

	/// <summary>
	/// 描画
	/// </summary>
	/// <param name="dxCore">DirectXコア</param>
	void Draw(DirectXCore* dxCore);

	// ===== ゲッター =====

	const Vector3& GetPosition() const { return position_; }
	bool IsActive() const { return isActive_; }

	/// <summary>
	/// 当たり判定用の半径
	/// </summary>
	float GetRadius() const { return radius_; }

	/// <summary>
	/// 命中時に呼ぶ（弾を非アクティブにする）
	/// </summary>
	void OnHit();

private:
	std::unique_ptr<Object3DInstance> model_;

	Vector3 position_{ 0.0f, 0.0f, 0.0f };
	Vector3 velocity_{ 0.0f, 0.0f, 0.0f };

	float speed_ = 15.0f;
	float radius_ = 0.25f;     // 当たり判定の半径
	float lifeTime_ = 3.0f;   // 生存時間（秒）
	float lifeTimer_ = 0.0f;  // 経過時間
	bool isActive_ = true;

	// 画面外判定の閾値（ゲーム範囲に合わせて調整）
	float outOfBoundsX_ = 50.0f;
	float outOfBoundsY_ = 30.0f;
};
