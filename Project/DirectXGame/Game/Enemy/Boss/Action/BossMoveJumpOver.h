#pragma once
#include <cmath>
#include "Enemy/Boss/IBossAction.h"
#include "IImGuiEditable.h"

/// <summary>
/// プレイヤーをまたぐように放物線で飛び越え、着地点はプレイヤーを挟んで反対側（=背後に回り込む）。
/// 着地目標は開始時のプレイヤー位置から一度だけ算出する（滞空中にプレイヤーが動いても追従しない＝
/// 見切りやすい弾道にする）。
/// </summary>
class BossMoveJumpOver : public IBossAction {
public:
	explicit BossMoveJumpOver(float duration = 0.8f, float jumpHeight = 6.0f, float landDistance = 6.0f)
		: duration_(duration), jumpHeight_(jumpHeight), landDistance_(landDistance) {}

	void OnEnter(BossActionContext& ctx) override {
		timer_    = 0.0f;
		finished_ = false;
		valid_    = false;
		ctx.billboardToPlayer = true;
		if (!ctx.boss || !ctx.player) return;

		const Vector3* bp = ctx.boss->GetEditableTranslate();
		const Vector3* pp = ctx.player->GetEditableTranslate();
		if (!bp || !pp) return;

		startPos_  = *bp;
		baselineY_ = bp->y;

		// ボス→プレイヤー方向（XZ）へ、プレイヤーを飛び越した先の着地点を一度だけ確定する。
		Vector3 d{ pp->x - bp->x, 0.0f, pp->z - bp->z };
		const float len = std::sqrt(d.x * d.x + d.z * d.z);
		if (len < 1e-4f) return;
		d.x /= len; d.z /= len;

		goalPos_ = {
			pp->x + d.x * landDistance_,
			baselineY_,
			pp->z + d.z * landDistance_,
		};
		valid_ = true;
	}

	void Update(float dt, BossActionContext& ctx) override {
		ctx.billboardToPlayer = true;
		if (!valid_ || !ctx.boss) { finished_ = true; return; }

		timer_ += dt;
		float t = (duration_ > 1e-4f) ? (timer_ / duration_) : 1.0f;
		if (t >= 1.0f) t = 1.0f;
		const float e = t * t * (3.0f - 2.0f * t); // smoothstep

		Vector3* bp = ctx.boss->GetEditableTranslate();
		if (!bp) { finished_ = true; return; }

		bp->x = startPos_.x + (goalPos_.x - startPos_.x) * e;
		bp->z = startPos_.z + (goalPos_.z - startPos_.z) * e;
		bp->y = baselineY_ + jumpHeight_ * std::sin(3.14159265f * t); // 放物線アーチ

		if (t >= 1.0f) {
			bp->x = goalPos_.x;
			bp->y = baselineY_;
			bp->z = goalPos_.z;
			finished_ = true;
		}
	}

	bool IsFinished() const override { return finished_; }

private:
	float duration_     = 0.8f;
	float jumpHeight_   = 6.0f;
	float landDistance_ = 6.0f;

	Vector3 startPos_{ 0.0f, 0.0f, 0.0f };
	Vector3 goalPos_{ 0.0f, 0.0f, 0.0f };
	float   baselineY_ = 0.0f;
	float   timer_     = 0.0f;
	bool    valid_      = false;
	bool    finished_   = false;
};
