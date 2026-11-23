#pragma once
#include "BaseTransition.h"
#include "Vector3.h"
#include <memory>

class SpriteInstance;

/// <summary>
/// シンプルなフェードイン/アウトトランジション
/// </summary>
class FadeTransition : public BaseTransition {
public:
	FadeTransition() = default;
	~FadeTransition() override = default;


	/// <summary>フェード用のスプライトを解放する</summary>
	void Finalize() override;
	/// <summary>不透明度が 0 より大きいときだけ、画面全体を覆う単色のスプライトを描く</summary>
	void Draw() override;

	/// <summary>トランジションの種類を返す</summary>
	/// <returns>TransitionType::Fade</returns>
	TransitionType GetType() const override { return TransitionType::Fade; }
	/// <summary>トランジション名を返す</summary>
	/// <returns>"Fade"</returns>
	std::string GetName() const override { return "Fade"; }

	/// <summary>覆う・開くそれぞれの長さを設定する</summary>
	/// <param name="duration">長さ [秒]</param>
	void SetFadeDuration(float duration) { fadeDuration_ = duration; }

	/// <summary>
	/// 次の 1 回だけ色と長さを変える（タイトルの扉に入る白フェード等）。
	/// 遷移が終わると黒・既定の長さに戻るので、他のシーンのフェードには影響しない。
	/// ChangeScene(..., TransitionType::Fade) の直前に呼ぶ。
	/// </summary>
	/// <param name="color">フェードの色（RGB 0〜1）</param>
	/// <param name="fadeDuration">覆う・開くそれぞれの長さ [秒]</param>
	/// <param name="holdDuration">隠したままにする最低の秒数 [秒]</param>
	void SetNextFade(const Vector3& color, float fadeDuration, float holdDuration);

protected:
	/// <summary>不透明度を 0 に戻す</summary>
	void OnStartCover() override;
	/// <summary>画面全体を覆う白 1x1 のスプライトを透明で作る</summary>
	void OnInitialize() override;
	/// <summary>経過時間に合わせて不透明度を 0→1 にする</summary>
	/// <param name="dt">経過 [秒]（使わない。経過は GetPhaseTime で見る）</param>
	/// <returns>不透明度が 1 になったら true</returns>
	bool UpdateCover(float dt) override;
	/// <summary>経過時間に合わせて不透明度を 1→0 にする</summary>
	/// <param name="dt">経過 [秒]（使わない。経過は GetPhaseTime で見る）</param>
	/// <returns>不透明度が 0 になったら true</returns>
	bool UpdateReveal(float dt) override;
	/// <summary>不透明度を 1 にする</summary>
	void SetFullyCovered() override;
	/// <summary>トランジション中なら色と不透明度をスプライトへ反映する</summary>
	void UpdateVisual() override;
	/// <summary>不透明度を 0 に戻し、SetNextFade の色と長さを既定（黒）に戻す</summary>
	void OnFinished() override;

private:
	static constexpr float kDefaultFadeDuration = 0.5f;
	static constexpr float kDefaultHoldDuration = 0.1f;

	std::unique_ptr<SpriteInstance> fadeSprite_;
	float fadeDuration_ = kDefaultFadeDuration;
	float alpha_ = 0.0f;
	Vector3 color_ = { 0.0f, 0.0f, 0.0f };
};
