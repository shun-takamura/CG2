#pragma once
#include <algorithm>
#include "MathUtility.h"
#include <cmath>
#include "Enemy/Boss/IBossState.h"
#include "IImGuiEditable.h"
#include "Camera.h"
#include "Effect/EffectManager.h"
#include "SoundManager.h"
#include "Scene/GameScene.h"

/// <summary>
/// 発狂移行の咆哮：攻撃はせず、画面を揺らし爆発エフェクトと白の激しい点滅で「ここから本気」を見せる。
/// 待機（BossIdleState）が行動の切れ目にだけ選ぶので、空中や突進の途中で割り込むことはない。
/// </summary>
class BossRageRoar : public BossActionState {
public:
	/// <summary>画面を揺らし、音と爆発エフェクトを出す</summary>
	/// <param name="ctx">ボス・自機・シーンと共有データ</param>
	void OnEnter(BossStateContext& ctx) override {
		timer_ = 0.0f;
		ctx.billboardToPlayer = true;
		if (ctx.scene) {
			if (Camera* cam = ctx.scene->GetCamera()) cam->Shake(kShakeIntensity, kDuration * kShakeDurationRate);
		}
		SoundManager::GetInstance()->Play2DSound("se_shockwave");
		if (ctx.boss) {
			if (const Vector3* bp = ctx.boss->GetEditableTranslate()) {
				if (auto* em = EffectManager::GetInstance()) em->Play("Explosion_00", *bp);
			}
		}
	}

	/// <summary>白の激しい点滅を、終わりに向けて弱めながら続ける</summary>
	/// <param name="dt">World の経過時間 [秒]</param>
	/// <param name="ctx">ボス・自機・シーンと共有データ</param>
	void Tick(float dt, BossStateContext& ctx) override {
		timer_ += dt;
		ctx.billboardToPlayer = true;
		// 終わりに向けて点滅を半分まで弱める
		const float fade = 1.0f - kBlinkFadeRate * (std::min)(timer_ / kDuration, 1.0f);
		const float blink = 0.5f + 0.5f * std::cos(kTwoPi * kBlinkHz * timer_);
		ctx.tint = { 1.0f, 1.0f, 1.0f, blink * fade };
	}

	/// <summary>咆哮が終わったか</summary>
	/// <returns>咆哮の長さ（kDuration）が過ぎたら true</returns>
	bool IsFinished() const override { return timer_ >= kDuration; }

private:
	static constexpr float kDuration          = 1.5f;   // 咆哮の長さ [秒]
	static constexpr float kShakeIntensity    = 0.35f;  // 画面の揺れの強さ
	static constexpr float kShakeDurationRate = 0.7f;   // 揺れの長さ（咆哮の長さに対する割合）
	static constexpr float kBlinkHz           = 8.0f;   // 白の点滅の速さ [回/秒]
	static constexpr float kBlinkFadeRate     = 0.5f;   // 終わりまでに点滅を弱める割合

	float timer_ = 0.0f;
};
