#pragma once
#include <algorithm>
#include "MathUtility.h"
#include <cmath>
#include "Enemy/Boss/IBossState.h"
#include "Enemy/Boss/BossRig.h"
#include "IImGuiEditable.h"
#include "Camera.h"
#include "Scene/GameScene.h"

/// <summary>
/// ひるみ（部位破壊の直後）：攻撃せず、沈んでよろめき、白く点滅する。残った腕はだらりと垂らす。
/// どの状態からでも IBossState::OnPartBroken でここへ移る。
/// </summary>
class BossStagger : public BossActionState {
public:
	/// <summary>ひるみを作る</summary>
	/// <param name="duration">ひるむ時間 [秒]</param>
	explicit BossStagger(float duration) : duration_(duration) {}

	/// <summary>戻す高さとして今の高さを控え、画面を揺らす（向きは自機へ追わない）</summary>
	/// <param name="ctx">ボス・自機・シーンと共有データ</param>
	void OnEnter(BossStateContext& ctx) override {
		timer_ = 0.0f;
		ctx.billboardToPlayer = false;
		if (const Vector3* bp = ctx.boss ? ctx.boss->GetEditableTranslate() : nullptr) baseY_ = bp->y;
		if (ctx.scene) {
			if (Camera* cam = ctx.scene->GetCamera()) cam->Shake(kShakeIntensity, kShakeDuration);
		}
	}

	/// <summary>沈み込んで戻り、左右によろめき、白く点滅し、腕を垂らす（どれも終わりに向けて弱める）</summary>
	/// <param name="dt">World の経過時間 [秒]</param>
	/// <param name="ctx">ボス・自機・シーンと共有データ</param>
	void Tick(float dt, BossStateContext& ctx) override {
		timer_ += dt;
		ctx.billboardToPlayer = false;
		const float t = (duration_ > kEpsilon) ? (std::min)(timer_ / duration_, 1.0f) : 1.0f;
		// 沈み込み → 戻る。左右に揺れる（揺れは終わりに向けて小さくする）
		if (Vector3* bp = ctx.boss ? ctx.boss->GetEditableTranslate() : nullptr) {
			bp->y = baseY_ - kSinkDepth * std::sin(kPi * t);
		}
		if (ctx.boss) {
			if (const Vector3* r = ctx.boss->GetEditableRotate()) {
				const float sway = std::sin(timer_ * kSwaySpeed) * kSwayAmplitude * (1.0f - t);
				ctx.boss->SetRotate({ r->x, r->y + sway, r->z });
			}
		}
		const float blink = 0.5f + 0.5f * std::cos(2.0f * kPi * kBlinkHz * timer_);
		ctx.tint = { 1.0f, 1.0f, 1.0f, blink * (1.0f - t) * kBlinkStrength };
		// 腕は前へ少し上げて内側へ垂らす
		if (ctx.rig) {
			ctx.rig->SetArmTarget(BossRig::Side::Left,  kArmRaise, kArmSwing, kArmSmoothTime);
			ctx.rig->SetArmTarget(BossRig::Side::Right, kArmRaise, kArmSwing, kArmSmoothTime);
		}
	}

	/// <summary>高さを元へ戻し、また自機の方を向くようにする</summary>
	/// <param name="ctx">ボス・自機・シーンと共有データ</param>
	void OnExit(BossStateContext& ctx) override {
		if (Vector3* bp = ctx.boss ? ctx.boss->GetEditableTranslate() : nullptr) bp->y = baseY_;
		ctx.billboardToPlayer = true;
	}

	/// <summary>ひるみが終わったか</summary>
	/// <returns>ひるむ時間が過ぎたら true</returns>
	bool IsFinished() const override { return timer_ >= duration_; }

private:
	static constexpr float kShakeIntensity = 0.25f;  // 画面の揺れの強さ
	static constexpr float kShakeDuration  = 0.4f;   // 画面の揺れの長さ [秒]
	static constexpr float kSinkDepth      = 0.8f;   // 沈み込む深さ [m]
	static constexpr float kSwaySpeed      = 14.0f;  // よろめきの速さ [rad/秒]
	static constexpr float kSwayAmplitude  = 0.02f;  // よろめきの振れ幅 [rad]
	static constexpr float kBlinkHz        = 6.0f;   // 白の点滅の速さ [回/秒]
	static constexpr float kBlinkStrength  = 0.8f;   // 白の点滅の濃さ
	static constexpr float kArmRaise       = 0.45f;  // 腕を前へ上げる角度 [rad]
	static constexpr float kArmSwing       = -0.3f;  // 腕を内側へ寄せる角度 [rad]
	static constexpr float kArmSmoothTime  = 0.15f;  // 腕が目標へ寄る時間 [秒]
	static constexpr float kEpsilon        = 1e-4f;

	float duration_ = 2.0f;
	float timer_    = 0.0f;
	float baseY_    = 0.0f;
};
