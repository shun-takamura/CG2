#pragma once
#include "Enemy/Boss/IBossState.h"
#include "MathUtility.h"
#include "Scene/GameScene.h"
#include "SoundManager.h"
#include "Enemy/Boss/BossRig.h"

/// <summary>
/// 叩きつけ：沈み込み（予兆）→プレイヤーの手前へ高く跳ぶ→着地の瞬間に地面すれすれの衝撃波リング（N 発）→硬直。
/// 着地点は跳ぶ瞬間に一度だけ確定する（空中で追尾しない＝着地点を見て離れればかわせる）。
/// プレイヤーの真上に着地すると衝撃波が発生と同時に当たって理不尽なので、手前 landShortDistance_ に降りる。
/// </summary>
class BossAttackSlam : public BossActionState {
	enum class Phase { Crouch, Jump, Recover, Done };

public:
	/// <summary>叩きつけを作る</summary>
	/// <param name="waveBulletCount">着地の衝撃波リングの弾数</param>
	/// <param name="crouchTime">沈み込み（予兆）の長さ [秒]</param>
	BossAttackSlam(int waveBulletCount = 24, float crouchTime = 0.5f)
		: waveBulletCount_(waveBulletCount), crouchTime_(crouchTime) {}

	/// <summary>沈み込みから始め、戻す高さとして今の高さを控える</summary>
	/// <param name="ctx">ボス・自機・シーンと共有データ</param>
	void OnEnter(BossStateContext& ctx) override {
		phase_ = Phase::Crouch;
		timer_ = 0.0f;
		ctx.billboardToPlayer = true;
		if (const Vector3* bp = ctx.boss ? ctx.boss->GetEditableTranslate() : nullptr) {
			baselineY_ = bp->y;
		}
	}

	/// <summary>沈み込み（腕を振り上げる）→放物線で跳ぶ→着地で衝撃波→硬直を 1 フレーム進める</summary>
	/// <param name="dt">World の経過時間 [秒]</param>
	/// <param name="ctx">ボス・自機・シーンと共有データ</param>
	void Tick(float dt, BossStateContext& ctx) override {
		ctx.billboardToPlayer = true;
		timer_ += dt;
		Vector3* bp = ctx.boss ? ctx.boss->GetEditableTranslate() : nullptr;
		if (!bp) { phase_ = Phase::Done; return; }

		if (phase_ == Phase::Crouch) {
			SetBossTelegraphTint(ctx, { 1.0f, 1.0f, 1.0f }, timer_, crouchTime_); // 弾幕系=黄 / 近接系=白
			SetArms(ctx, kCrouchRaise, kCrouchSwing, kCrouchSmoothTime); // 両腕を振り上げる
			float t = (crouchTime_ > kMinDuration) ? (timer_ / crouchTime_) : 1.0f;
			if (t > 1.0f) t = 1.0f;
			bp->y = baselineY_ - crouchDepth_ * std::sin(kPi * 0.5f * t);
			if (t >= 1.0f) {
				if (!BeginJump(ctx)) { bp->y = baselineY_; phase_ = Phase::Done; return; }
				phase_ = Phase::Jump;
				timer_ = 0.0f;
			}
		} else if (phase_ == Phase::Jump) {
			float t = (jumpDuration_ > kMinDuration) ? (timer_ / jumpDuration_) : 1.0f;
			if (t > 1.0f) t = 1.0f;
			// 上がりきるまで振り上げたまま、落ち始めたら振り下ろしへ
			if (t < kArmSwingDownStartRate) SetArms(ctx, kJumpUpRaise, kJumpUpSwing, kJumpUpSmoothTime);
			else SetArms(ctx, kJumpDownRaise, kJumpDownSwing, kJumpDownSmoothTime);
			bp->x = startPos_.x + (goalPos_.x - startPos_.x) * t;
			bp->z = startPos_.z + (goalPos_.z - startPos_.z) * t;
			// 沈んだ位置から跳び、元の高さに着地する放物線
			const float base = (baselineY_ - crouchDepth_) + crouchDepth_ * t;
			bp->y = base + jumpHeight_ * std::sin(kPi * t);
			if (t >= 1.0f) {
				bp->y = baselineY_;
				FireShockwave(ctx, *bp);
				phase_ = Phase::Recover;
				timer_ = 0.0f;
			}
		} else if (phase_ == Phase::Recover) {
			// 叩きつけた腕を少しの間そのまま（地面を叩いた手応え）
			if (timer_ < recoverTime_ * 0.5f) SetArms(ctx, kLandRaise, kLandSwing, kLandSmoothTime);
			if (timer_ >= recoverTime_) phase_ = Phase::Done;
		}
	}

	/// <summary>沈み込み・空中の途中で打ち切られたら高さを元へ戻す</summary>
	/// <param name="ctx">ボス・自機・シーンと共有データ</param>
	void OnExit(BossStateContext& ctx) override {
		// 途中中断でも沈み込み/空中の高さを残さない
		if (phase_ != Phase::Recover && phase_ != Phase::Done) {
			if (Vector3* bp = ctx.boss ? ctx.boss->GetEditableTranslate() : nullptr) bp->y = baselineY_;
		}
	}

	/// <summary>叩きつけが終わったか</summary>
	/// <returns>硬直まで終わったら true（ボスが無い・跳べなかった時も true）</returns>
	bool IsFinished() const override { return phase_ == Phase::Done; }

private:
	// 沈み込み：両腕を振り上げる構え [rad]
	static constexpr float kCrouchRaise = 2.7f;
	// 沈み込み：両腕の振り [rad]
	static constexpr float kCrouchSwing = -0.3f;
	// 沈み込み：構えへ寄る時間 [秒]
	static constexpr float kCrouchSmoothTime = 0.12f;
	// 跳び上がり：振り上げたままにする間（跳躍の長さに対する割合）
	static constexpr float kArmSwingDownStartRate = 0.6f;
	// 跳び上がり：両腕を振り上げる構え [rad]
	static constexpr float kJumpUpRaise = 2.9f;
	// 跳び上がり：両腕の振り [rad]
	static constexpr float kJumpUpSwing = -0.2f;
	// 跳び上がり：構えへ寄る時間 [秒]
	static constexpr float kJumpUpSmoothTime = 0.1f;
	// 落下：両腕を振り下ろす構え [rad]
	static constexpr float kJumpDownRaise = 0.6f;
	// 落下：両腕の振り [rad]
	static constexpr float kJumpDownSwing = 0.3f;
	// 落下：構えへ寄る時間 [秒]
	static constexpr float kJumpDownSmoothTime = 0.06f;
	// 着地後：叩きつけた腕の構え [rad]
	static constexpr float kLandRaise = 0.55f;
	// 着地後：両腕の振り [rad]
	static constexpr float kLandSwing = 0.3f;
	// 着地後：構えへ寄る時間 [秒]
	static constexpr float kLandSmoothTime = 0.04f;
	// これより短い時間は最初から終わりとみなす [秒]
	static constexpr float kMinDuration = 1e-4f;

	/// <summary>着地点（自機の手前、アリーナ内）と衝撃波の高さを確定する</summary>
	/// <param name="ctx">ボス・自機・シーンと共有データ</param>
	/// <returns>確定できたら true。照準が取れなければ false</returns>
	bool BeginJump(BossStateContext& ctx) {
		const BossAimInfo aim = ComputeBossAim(ctx);
		if (!aim.valid) return false;
		startPos_ = aim.origin;
		const float len = (std::max)(0.0f, aim.distXZ - landShortDistance_);
		goalPos_ = {
			startPos_.x + std::sin(aim.yaw) * len,
			baselineY_,
			startPos_.z + std::cos(aim.yaw) * len,
		};
		ClampToBossArena(ctx, goalPos_);
		// 衝撃波はプレイヤーの胸の高さを水平に走らせる（地面すれすれ＝地上のプレイヤーに当たる高さ）
		const Vector3* pp = ctx.player ? ctx.player->GetEditableTranslate() : nullptr;
		shockwaveY_ = pp ? pp->y + shockwaveHeight_ : baselineY_;
		return true;
	}

	/// <summary>両腕に同じ構えを指示する。手足が無ければ何もしない</summary>
	/// <param name="ctx">ボス・自機・シーンと共有データ</param>
	/// <param name="raise">構え [ラジアン]</param>
	/// <param name="swing">振り [ラジアン]</param>
	/// <param name="smoothTime">目標の構えへ寄る時間 [秒]</param>
	static void SetArms(BossStateContext& ctx, float raise, float swing, float smoothTime) {
		if (!ctx.rig) return;
		ctx.rig->SetArmTarget(BossRig::Side::Left, raise, swing, smoothTime);
		ctx.rig->SetArmTarget(BossRig::Side::Right, raise, swing, smoothTime);
	}

	/// <summary>着地点から自機の胸の高さを水平に広がる衝撃波リングを撃ち、音を鳴らす</summary>
	/// <param name="ctx">ボス・自機・シーンと共有データ</param>
	/// <param name="landPos">着地点（ワールド）</param>
	void FireShockwave(BossStateContext& ctx, const Vector3& landPos) {
		if (!ctx.scene || waveBulletCount_ <= 0) return;
		SoundManager::GetInstance()->Play2DSound("se_shockwave");
		const Vector3 origin{ landPos.x, shockwaveY_, landPos.z };
		const float step = kTwoPi / static_cast<float>(waveBulletCount_);
		for (int i = 0; i < waveBulletCount_; ++i) {
			const Vector3 dir = BossDirFromYawPitch(step * static_cast<float>(i), 0.0f);
			ctx.scene->SpawnEnemyBullet(origin, dir, shockwaveSpeed_, -1.0f, "EnemyBullet", nullptr, 0.0f);
		}
	}

	Phase phase_ = Phase::Crouch;
	float timer_ = 0.0f;

	int   waveBulletCount_ = 24;
	float crouchTime_ = 0.5f;
	float crouchDepth_ = 0.6f;
	float jumpDuration_ = 0.9f;
	float jumpHeight_ = 10.0f;
	float landShortDistance_ = 5.0f;  // プレイヤーの手前に降りる距離 [m]
	float shockwaveHeight_ = 1.0f;    // プレイヤー足元からの衝撃波の高さ [m]
	float shockwaveSpeed_ = 25.0f;    // 衝撃波は通常弾より遅く、見て避けられる速さ
	float recoverTime_ = 1.2f;

	float baselineY_ = 0.0f;
	float shockwaveY_ = 0.0f;
	Vector3 startPos_{ 0.0f, 0.0f, 0.0f };
	Vector3 goalPos_{ 0.0f, 0.0f, 0.0f };
};
