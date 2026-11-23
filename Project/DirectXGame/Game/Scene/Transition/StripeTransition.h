#pragma once
#include "BaseTransition.h"
#include "SpriteInstance.h"
#include <vector>
#include <memory>

class SpriteInstance;

/// <summary>
/// スマブラ風斜めストライプトランジション
/// </summary>
class StripeTransition : public BaseTransition {
public:
	StripeTransition() = default;
	~StripeTransition() override = default;

	/// <summary>帯のスプライトと進行度を捨てる</summary>
	void Finalize() override;
	/// <summary>帯のスプライトを描く</summary>
	void Draw() override;

	/// <summary>トランジションの種類を返す</summary>
	/// <returns>TransitionType::Stripe</returns>
	TransitionType GetType() const override { return TransitionType::Stripe; }
	/// <summary>トランジション名を返す</summary>
	/// <returns>"Stripe"</returns>
	std::string GetName() const override { return "Stripe"; }

	/// <summary>帯の本数を設定する（次に覆い始めるときに作り直す）</summary>
	/// <param name="count">帯の本数</param>
	void SetStripeCount(int count) { stripeCount_ = count; needsRecreate_ = true; }
	/// <summary>帯 1 本が入り切る（はけ切る）までの時間を設定する</summary>
	/// <param name="duration">時間 [秒]</param>
	void SetTransitionDuration(float duration) { transitionDuration_ = duration; }
	/// <summary>帯が動く向きの傾きを設定する（真下から何度傾けて動かすか。負で左下へ）</summary>
	/// <param name="angle">角度 [度]</param>
	void SetStripeAngle(float angle) { stripeAngle_ = angle; }
	/// <summary>隣の帯が動き始めるまでの遅れを設定する</summary>
	/// <param name="delay">遅れ [秒]</param>
	void SetStripeDelay(float delay) { stripeDelay_ = delay; }

	/// <summary>帯の位置・大きさの調整とテスト表示を ImGui で行う</summary>
	void OnImGui();

protected:
	/// <summary>必要なら帯を作り直し、全部の帯の進行度を 0 に戻す</summary>
	void OnStartCover() override;
	/// <summary>帯の大きさを画面サイズの比率で決めて帯を作る</summary>
	void OnInitialize() override;
	/// <summary>帯を stripeDelay_ 秒ずつずらして動かし始め、transitionDuration_ 秒かけて右上から入って画面を覆わせる</summary>
	/// <param name="dt">経過 [秒]（使わない。覆い始めてからの経過秒 GetPhaseTime で進める）</param>
	/// <returns>全部の帯が覆い切ったら true</returns>
	bool UpdateCover(float dt) override;
	/// <summary>帯を stripeDelay_ 秒ずつずらして動かし始め、transitionDuration_ 秒かけて左下へはけさせる</summary>
	/// <param name="dt">経過 [秒]（使わない。開き始めてからの経過秒 GetPhaseTime で進める）</param>
	/// <returns>全部の帯がはけ切ったら true</returns>
	bool UpdateReveal(float dt) override;
	/// <summary>必要なら帯を作り直し、全部の帯の進行度を 1 にする</summary>
	void SetFullyCovered() override;
	/// <summary>トランジション中（またはデバッグの強制表示中）なら帯の位置を更新する</summary>
	void UpdateVisual() override;

private:
	/// <summary>stripe.dds のスプライトを帯の本数だけ作り直す</summary>
	void CreateStripes();
	/// <summary>各帯の進行度から位置を求めてスプライトへ設定する（入りは右上から、はけは左下へ。動く向きは stripeAngle_）</summary>
	void UpdateStripePositions();
	/// <summary>i 本目の帯の進行度を、動き始めてからの経過秒から求める</summary>
	/// <param name="index">帯の番号（0 から。番号が大きいほど stripeDelay_ ずつ遅れて動き始める）</param>
	/// <returns>進行度（0〜1。まだ動き始めていなければ 0）</returns>
	float CalcStripeProgress(size_t index) const;

	// 帯の設定
	int stripeCount_ = 8;
	// 帯が動く向き：真下から何度傾けるか [度]（-30 で、下へ 1 進むごとに左へ tan30° 進む）
	float stripeAngle_ = -30.0f;
	float stripeWidth_ = 0.0f;
	// 帯 1 本が入り切る（はけ切る）までの時間 [秒]
	float transitionDuration_ = 0.17f;
	// 隣の帯が動き始めるまでの遅れ [秒]
	float stripeDelay_ = 0.1f;
	float spacingOffset_ = 90.0f;

	// 再生成フラグ
	bool needsRecreate_ = false;

	// 各帯のスプライト
	std::vector<std::unique_ptr<SpriteInstance>> stripes_;

	// 各帯の進行度（0.0〜1.0）
	std::vector<float> stripeProgress_;

	// デバッグ用：各帯の位置を保存
	std::vector<Vector2> stripePositions_;

	// ImGui調整用パラメータ
	float debugStripeWidth_ = 210.0f;
	float debugStripeHeight_ = 920.0f;
	bool debugForceShow_ = false;  // 強制表示フラグ
};