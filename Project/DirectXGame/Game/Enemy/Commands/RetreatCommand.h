#pragma once
#include <cmath>
#include "Enemy/IEnemyCommand.h"
#include "Enemy/EnemyContext.h"

/// <summary>
/// 敵を一定ベクトルで画面外へ退避させて消滅させる。
/// OnEnter でスプライン追従を切り離し、freeVelocity を設定する。
/// 指定距離を移動したら IsFinished() → GameScene がエンティティを破棄する。
/// </summary>
class RetreatCommand : public IEnemyCommand {
public:
	explicit RetreatCommand(
		Vector3 direction = { 0.0f, 0.8f, -0.6f }, // 画面後方+上方向（正規化済みで渡す）
		float speed = 25.0f,
		float maxDistance = 80.0f)
		: speed_(speed), maxDistance_(maxDistance) {
		const float len = std::sqrt(
			direction.x * direction.x + direction.y * direction.y + direction.z * direction.z);
		if (len > 0.001f)
			dir_ = { direction.x / len, direction.y / len, direction.z / len };
		else
			dir_ = { 0.0f, 1.0f, 0.0f };
	}

	void OnEnter(IImGuiEditable*, EnemyContext& ctx) override {
		traveled_           = 0.0f;
		ctx.requestDetach   = true;
		ctx.useFreeVelocity = true;
		ctx.freeVelocity    = { dir_.x * speed_, dir_.y * speed_, dir_.z * speed_ };
	}

	void Update(float dt, IImGuiEditable*, EnemyContext& ctx) override {
		traveled_ += speed_ * dt;
		ctx.useFreeVelocity = true;
		ctx.freeVelocity    = { dir_.x * speed_, dir_.y * speed_, dir_.z * speed_ };
	}

	bool IsFinished() const override { return traveled_ >= maxDistance_; }

private:
	Vector3 dir_{ 0.0f, 1.0f, 0.0f };
	float   speed_       = 25.0f;
	float   maxDistance_ = 80.0f;
	float   traveled_    = 0.0f;
};
