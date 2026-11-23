#pragma once
#include <cmath>
#include "Enemy/Commands/IEnemyCommand.h"
#include "Enemy/EnemyContext.h"

/// <summary>
/// 敵を一定ベクトルで画面外へ退避させて消滅させる。
/// OnEnter でスプライン追従を切り離し、freeVelocity を設定する。
/// 指定距離を移動したら IsFinished() → GameScene がエンティティを破棄する。
/// </summary>
class RetreatCommand : public IEnemyCommand {
public:
	/// <summary>退避のコマンドを作る</summary>
	/// <param name="direction">退避する向き（中で正規化する。長さがほぼ 0 なら真上）</param>
	/// <param name="speed">退避の速さ [m/秒]</param>
	/// <param name="maxDistance">この距離 [m] 進んだら終わる</param>
	explicit RetreatCommand(
		Vector3 direction = { 0.0f, 0.8f, -0.6f }, // 画面後方+上方向（正規化済みで渡す）
		float speed = 25.0f,
		float maxDistance = 80.0f)
		: speed_(speed), maxDistance_(maxDistance) {
		const float len = std::sqrt(
			direction.x * direction.x + direction.y * direction.y + direction.z * direction.z);
		if (len > kNearZeroLength)
			dir_ = { direction.x / len, direction.y / len, direction.z / len };
		else
			dir_ = { 0.0f, 1.0f, 0.0f };
	}

	/// <summary>スプライン追従を切り離し、退避の速度を自由移動として設定する</summary>
	/// <param name="entity">動かす敵（このコマンドでは使わない）</param>
	/// <param name="ctx">敵の入力と結果の書き込み先</param>
	void OnEnter([[maybe_unused]] IImGuiEditable* entity, EnemyContext& ctx) override {
		traveled_           = 0.0f;
		ctx.requestDetach   = true;
		ctx.useFreeVelocity = true;
		ctx.freeVelocity    = { dir_.x * speed_, dir_.y * speed_, dir_.z * speed_ };
	}

	/// <summary>退避の速度を書き続け、進んだ距離を数える</summary>
	/// <param name="dt">経過時間 [秒]</param>
	/// <param name="ctx">敵の入力と結果の書き込み先</param>
	void Update(float dt, IImGuiEditable*, EnemyContext& ctx) override {
		traveled_ += speed_ * dt;
		ctx.useFreeVelocity = true;
		ctx.freeVelocity    = { dir_.x * speed_, dir_.y * speed_, dir_.z * speed_ };
	}

	/// <summary>退避が終わったか（終わったら GameScene が敵を破棄する）</summary>
	/// <returns>maxDistance 進んだら true</returns>
	bool IsFinished() const override { return traveled_ >= maxDistance_; }

private:
	// これより短い向きは使わず真上へ退避する
	static constexpr float kNearZeroLength = 0.001f;
	Vector3 dir_{ 0.0f, 1.0f, 0.0f };
	float   speed_       = 25.0f;
	float   maxDistance_ = 80.0f;
	float   traveled_    = 0.0f;
};
