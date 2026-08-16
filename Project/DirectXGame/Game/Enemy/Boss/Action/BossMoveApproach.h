#pragma once
#include <cmath>
#include "Enemy/Boss/IBossAction.h"
#include "IImGuiEditable.h"

/// <summary>
/// プレイヤーへ向けて一定時間だけ接近する移動。stopDistance_ まで縮まったら早期終了する。
/// </summary>
class BossMoveApproach : public IBossAction {
public:
	explicit BossMoveApproach(float duration = 1.2f, float speed = 6.0f, float stopDistance = 8.0f)
		: duration_(duration), speed_(speed), stopDistance_(stopDistance) {}

	void OnEnter(BossActionContext& ctx) override {
		timer_    = 0.0f;
		finished_ = false;
		ctx.billboardToPlayer = true;
	}

	void Update(float dt, BossActionContext& ctx) override {
		ctx.billboardToPlayer = true;
		timer_ += dt;
		if (timer_ >= duration_) { finished_ = true; return; }
		if (!ctx.boss || !ctx.player) { finished_ = true; return; }

		Vector3* bp = ctx.boss->GetEditableTranslate();
		const Vector3* pp = ctx.player->GetEditableTranslate();
		if (!bp || !pp) { finished_ = true; return; }

		Vector3 d{ pp->x - bp->x, pp->y - bp->y, pp->z - bp->z };
		d.y = 0.0f;
		const float len = std::sqrt(d.x * d.x + d.z * d.z);
		if (len <= stopDistance_ || len < 1e-4f) { finished_ = true; return; }

		d.x /= len; d.z /= len;
		bp->x += d.x * speed_ * dt;
		bp->z += d.z * speed_ * dt;
	}

	bool IsFinished() const override { return finished_; }

private:
	float duration_     = 1.2f;
	float speed_        = 6.0f;
	float stopDistance_ = 8.0f;
	float timer_        = 0.0f;
	bool  finished_      = false;
};
