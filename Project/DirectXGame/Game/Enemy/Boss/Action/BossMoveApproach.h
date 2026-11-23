#pragma once
#include <cmath>
#include "Enemy/Boss/IBossState.h"
#include "IImGuiEditable.h"

/// <summary>
/// プレイヤーへ向けて一定時間だけ接近する移動。stopDistance_ まで縮まったら早期終了する。
/// </summary>
class BossMoveApproach : public BossActionState {
public:
	/// <summary>接近の移動を作る</summary>
	/// <param name="duration">接近する最長の時間 [秒]</param>
	/// <param name="speed">移動の速さ [m/秒]</param>
	/// <param name="stopDistance">この XZ 距離 [m] まで縮まったら止める</param>
	explicit BossMoveApproach(float duration = 1.2f, float speed = 6.0f, float stopDistance = 8.0f)
		: duration_(duration), speed_(speed), stopDistance_(stopDistance) {}

	/// <summary>タイマーと終了フラグを戻し、自機の方を向く</summary>
	/// <param name="ctx">ボス・自機・シーンと共有データ</param>
	void OnEnter(BossStateContext& ctx) override {
		timer_    = 0.0f;
		finished_ = false;
		ctx.billboardToPlayer = true;
	}

	/// <summary>自機へ向けて XZ で近づく。時間切れか停止距離に入ったら終わる</summary>
	/// <param name="dt">World の経過時間 [秒]</param>
	/// <param name="ctx">ボス・自機・シーンと共有データ</param>
	void Tick(float dt, BossStateContext& ctx) override {
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
		if (len <= stopDistance_ || len < kNearZeroLength) { finished_ = true; return; }

		d.x /= len; d.z /= len;
		bp->x += d.x * speed_ * dt;
		bp->z += d.z * speed_ * dt;
	}

	/// <summary>接近が終わったか</summary>
	/// <returns>時間切れ・停止距離に入った・ボスか自機が無い時に true</returns>
	bool IsFinished() const override { return finished_; }

private:
	// これより自機に近いときは向きが決まらないので終わる [m]
	static constexpr float kNearZeroLength = 1e-4f;
	float duration_     = 1.2f;
	float speed_        = 6.0f;
	float stopDistance_ = 8.0f;
	float timer_        = 0.0f;
	bool  finished_      = false;
};
