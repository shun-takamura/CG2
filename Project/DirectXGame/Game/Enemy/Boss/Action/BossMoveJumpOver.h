#pragma once
#include <cmath>
#include "MathUtility.h"
#include "Enemy/Boss/IBossState.h"
#include "IImGuiEditable.h"

/// <summary>
/// プレイヤーをまたぐように放物線で飛び越え、着地点はプレイヤーを挟んで反対側（=背後に回り込む）。
/// 着地目標は開始時のプレイヤー位置から一度だけ算出する（滞空中にプレイヤーが動いても追従しない＝
/// 見切りやすい弾道にする）。
/// </summary>
class BossMoveJumpOver : public BossActionState {
public:
	/// <summary>飛び越えの移動を作る</summary>
	/// <param name="duration">滞空時間 [秒]</param>
	/// <param name="jumpHeight">放物線の頂点の高さ [m]</param>
	/// <param name="landDistance">自機を越えた先の着地までの距離 [m]</param>
	explicit BossMoveJumpOver(float duration = 0.8f, float jumpHeight = 6.0f, float landDistance = 6.0f)
		: duration_(duration), jumpHeight_(jumpHeight), landDistance_(landDistance) {}

	/// <summary>始点と、自機を挟んで反対側の着地点（アリーナ内）を一度だけ確定する</summary>
	/// <param name="ctx">ボス・自機・シーンと共有データ</param>
	void OnEnter(BossStateContext& ctx) override {
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
		if (len < kNearZeroLength) return;
		d.x /= len; d.z /= len;

		goalPos_ = {
			pp->x + d.x * landDistance_,
			baselineY_,
			pp->z + d.z * landDistance_,
		};
		ClampToBossArena(ctx, goalPos_);
		valid_ = true;
	}

	/// <summary>放物線のアーチで着地点へ進め、着地したら終わる</summary>
	/// <param name="dt">World の経過時間 [秒]</param>
	/// <param name="ctx">ボス・自機・シーンと共有データ</param>
	void Tick(float dt, BossStateContext& ctx) override {
		ctx.billboardToPlayer = true;
		if (!valid_ || !ctx.boss) { finished_ = true; return; }

		timer_ += dt;
		float t = (duration_ > kMinDuration) ? (timer_ / duration_) : 1.0f;
		if (t >= 1.0f) t = 1.0f;
		const float e = t * t * (3.0f - 2.0f * t); // smoothstep

		Vector3* bp = ctx.boss->GetEditableTranslate();
		if (!bp) { finished_ = true; return; }

		bp->x = startPos_.x + (goalPos_.x - startPos_.x) * e;
		bp->z = startPos_.z + (goalPos_.z - startPos_.z) * e;
		bp->y = baselineY_ + jumpHeight_ * std::sin(kPi * t); // 放物線アーチ

		if (t >= 1.0f) {
			bp->x = goalPos_.x;
			bp->y = baselineY_;
			bp->z = goalPos_.z;
			finished_ = true;
		}
	}

	/// <summary>飛び越えが終わったか</summary>
	/// <returns>着地した・着地点を決められなかった時に true</returns>
	bool IsFinished() const override { return finished_; }

private:
	// これより自機に近いときは向きが決まらないので跳ばない [m]
	static constexpr float kNearZeroLength = 1e-4f;
	// これより短い時間は最初から終わりとみなす [秒]
	static constexpr float kMinDuration = 1e-4f;

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
