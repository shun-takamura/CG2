#include "StageClearBanner.h"
#include "MathUtility.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

#include "Text/TextRenderer.h"
#include "Vector2.h"
#include "Vector4.h"

#ifdef _DEBUG
#include "imgui.h"
#endif

namespace {
	// これより短い区間は start を過ぎたら終わりとみなす [秒]
	constexpr float kMinPhaseLength = 1e-4f;
	// 帯の色（濃い紺）
	constexpr float kBandColorRed = 0.02f;
	constexpr float kBandColorGreen = 0.03f;
	constexpr float kBandColorBlue = 0.06f;
	// 金の線の太さ [px]
	constexpr float kLineThickness = 2.0f;
	// 線の先の点の大きさ [px] と、その半分 [px]（点の中心を線の先に合わせる）
	constexpr float kDotSize = 6.0f;
	constexpr float kDotHalfSize = 3.0f;
	// 上の点の上端の、線の隙間からの高さ [px]
	constexpr float kTopDotOffset = 4.0f;
	// 下の点の上端の、線の隙間からのずれ [px]
	constexpr float kBottomDotOffset = 2.0f;
	// 文字が出きるまでの速さ（字間が詰まる進み具合に対する倍率）
	constexpr float kTextFadeInRate = 1.6f;
	// 詰まりきった直後に白く光る時間 [秒]
	constexpr float kFlashDuration = 0.25f;
	// 文字の色（淡い金）の緑・青の成分
	constexpr float kTextColorGreen = 0.94f;
	constexpr float kTextColorBlue = 0.76f;
	// 文字のアウトラインの太さ [px] と不透明度
	constexpr float kTextOutlineThickness = 2.5f;
	constexpr float kTextOutlineAlpha = 0.85f;
	// 光の筋の色の緑・青の成分
	constexpr float kSweepColorGreen = 0.97f;
	constexpr float kSweepColorBlue = 0.88f;
	// 光の筋の外側の縁の不透明度の倍率
	constexpr float kSweepOuterAlphaRate = 0.4f;
	// 光の筋の内側の縁の半分の幅（筋の幅に対する割合）と不透明度の倍率
	constexpr float kSweepInnerHalfWidthRate = 0.25f;
	constexpr float kSweepInnerAlphaRate = 0.6f;
	// 光の筋の芯の半分の幅・幅（筋の幅に対する割合）
	constexpr float kSweepCoreHalfWidthRate = 0.08f;
	constexpr float kSweepCoreWidthRate = 0.16f;
	// 1 秒あたりの 1/100 秒の数（小数で計算するとき）
	constexpr float kCentisecondsPerSecondF = 100.0f;
	// 1 秒あたりの 1/100 秒の数
	constexpr int kCentisecondsPerSecond = 100;
	// 1 分あたりの 1/100 秒の数
	constexpr int kCentisecondsPerMinute = 6000;
	// 1 分あたりの秒数
	constexpr int kSecondsPerMinute = 60;
	// 表示できる分の上限（2 桁）
	constexpr int kMaxDisplayMinutes = 99;
	// クリアタイムの文字バッファの大きさ [文字]
	constexpr int kTimeTextBufferSize = 48;
	// クリアタイムの、下の線からの空き [px]
	constexpr float kSubTextGap = 16.0f;
	// クリアタイムが出始めるときの下からのずれ [px]
	constexpr float kSubTextRise = 8.0f;
	// クリアタイムのアウトラインの太さ [px] と不透明度
	constexpr float kSubOutlineThickness = 1.5f;
	constexpr float kSubOutlineAlpha = 0.8f;

	/// <summary>0..1 に収める</summary>
	/// <param name="x">値</param>
	/// <returns>0..1 に収めた値</returns>
	float Clamp01(float x) { return (std::clamp)(x, 0.0f, 1.0f); }
	/// <summary>0..1 の smoothstep</summary>
	/// <param name="x">値（0..1 に収める）</param>
	/// <returns>なめらかに補間した値（0..1）</returns>
	float SmoothStep01(float x) { x = Clamp01(x); return x * x * (3.0f - 2.0f * x); }
	/// <summary>0..1 の ease-out（3 次）</summary>
	/// <param name="x">値（0..1 に収める）</param>
	/// <returns>補間した値（0..1）</returns>
	float EaseOutCubic(float x) { x = Clamp01(x); const float u = 1.0f - x; return 1.0f - u * u * u; }
	/// <summary>区間 [start, start+len] での進み具合を求める</summary>
	/// <param name="t">経過時間 [秒]</param>
	/// <param name="start">区間の始まり [秒]</param>
	/// <param name="len">区間の長さ [秒]（ほぼ 0 なら start を過ぎたら 1）</param>
	/// <returns>進み具合（0..1）</returns>
	float Phase(float t, float start, float len) { return len <= kMinPhaseLength ? (t >= start ? 1.0f : 0.0f) : Clamp01((t - start) / len); }
}

void StageClearBanner::Start(float clearTimeSec) {
	active_ = true;
	time_ = 0.0f;
	clearTime_ = clearTimeSec;
}

void StageClearBanner::Reset() {
	active_ = false;
	time_ = 0.0f;
}

void StageClearBanner::Update(float dt) {
	if (active_) time_ += dt;
}

void StageClearBanner::Draw(float screenW, float screenH) const {
	if (!active_) return;
	TextRenderer* tr = TextRenderer::GetInstance();
	if (!tr->IsInitialized()) return;

	const float cy = screenH * centerYRatio_;
	const float bandTop = cy - bandHeight_ * 0.5f;
	const float bandBottom = cy + bandHeight_ * 0.5f;

	// ----- 帯：中央から左右へ伸びる -----
	const float bandOpen = EaseOutCubic(Phase(time_, 0.0f, bandOpenTime_));
	if (bandOpen > 0.0f) {
		const float w = screenW * bandOpen;
		tr->DrawRect({ (screenW - w) * 0.5f, bandTop }, { w, bandHeight_ }, { kBandColorRed, kBandColorGreen, kBandColorBlue, bandAlpha_ * bandOpen });
	}

	// ----- 金の線：上下の縁を、上は右へ・下は左へ走る -----
	const Vector4 gold{ 1.0f, 0.82f, 0.42f, 1.0f };
	const float lineP = EaseOutCubic(Phase(time_, lineStart_, lineTime_));
	if (lineP > 0.0f) {
		const float full = screenW * lineWidthRatio_;
		const float left = (screenW - full) * 0.5f;
		const float len = full * lineP;
		tr->DrawRect({ left, bandTop - lineGap_ - kLineThickness }, { len, kLineThickness }, gold);
		tr->DrawRect({ left + full - len, bandBottom + lineGap_ }, { len, kLineThickness }, gold);
		// 線の先の小さな菱形の代わりの点
		tr->DrawRect({ left + len - kDotHalfSize, bandTop - lineGap_ - kTopDotOffset }, { kDotSize, kDotSize }, gold);
		tr->DrawRect({ left + full - len - kDotHalfSize, bandBottom + lineGap_ - kBottomDotOffset }, { kDotSize, kDotSize }, gold);
	}

	// ----- 文字：字間が詰まりながら出る。詰まりきる所で一瞬白く -----
	constexpr const char* kText = "STAGE CLEAR";
	const float textP = Phase(time_, textStart_, textTime_);
	if (textP > 0.0f) {
		const float spacing = textSpacingFrom_ + (textSpacingTo_ - textSpacingFrom_) * EaseOutCubic(textP);
		const float alpha = SmoothStep01(textP * kTextFadeInRate);
		// 詰まりきった直後（textP=1 から 0.25 秒）に白へ寄せて戻す
		const float landT = time_ - (textStart_ + textTime_);
		const float flash = (landT >= 0.0f && landT < kFlashDuration) ? (1.0f - landT / kFlashDuration) : 0.0f;
		const Vector4 base{ 1.0f, kTextColorGreen, kTextColorBlue, alpha };
		const Vector4 color{
			base.x + (1.0f - base.x) * flash,
			base.y + (1.0f - base.y) * flash,
			base.z + (1.0f - base.z) * flash,
			alpha };
		const std::string text = kText;
		float total = 0.0f;
		for (size_t i = 0; i < text.size(); ++i) {
			total += tr->MeasureWidth(text.substr(i, 1), textScale_);
			if (i + 1 < text.size()) total += spacing;
		}
		float x = (screenW - total) * 0.5f;
		const float y = cy - textFontPx_ * textScale_ * 0.5f;
		for (size_t i = 0; i < text.size(); ++i) {
			const std::string ch = text.substr(i, 1);
			if (ch != " ") tr->DrawText(ch, { x, y }, textScale_, color, kTextOutlineThickness, { 0.0f, 0.0f, 0.0f, alpha * kTextOutlineAlpha });
			x += tr->MeasureWidth(ch, textScale_) + spacing;
		}
	}

	// ----- 光の筋：帯の中を左から右へ（文字の上に薄く重ねる。発光の層は使わない）-----
	const float sweepP = Phase(time_, sweepStart_, sweepTime_);
	if (sweepP > 0.0f && sweepP < 1.0f) {
		const float full = screenW * lineWidthRatio_;
		const float left = (screenW - full) * 0.5f - sweepWidth_;
		const float x = left + (full + sweepWidth_) * SmoothStep01(sweepP);
		const float fade = std::sin(sweepP * kPi);
		// 芯と、その両側の薄い縁
		tr->DrawRect({ x - sweepWidth_ * 0.5f, bandTop }, { sweepWidth_, bandHeight_ }, { 1.0f, kSweepColorGreen, kSweepColorBlue, sweepAlpha_ * kSweepOuterAlphaRate * fade });
		tr->DrawRect({ x - sweepWidth_ * kSweepInnerHalfWidthRate, bandTop }, { sweepWidth_ * 0.5f, bandHeight_ }, { 1.0f, kSweepColorGreen, kSweepColorBlue, sweepAlpha_ * kSweepInnerAlphaRate * fade });
		tr->DrawRect({ x - sweepWidth_ * kSweepCoreHalfWidthRate, bandTop }, { sweepWidth_ * kSweepCoreWidthRate, bandHeight_ }, { 1.0f, 1.0f, 1.0f, sweepAlpha_ * fade });
	}

	// ----- クリアタイム（帯の下の線のさらに下）-----
	const float subP = SmoothStep01(Phase(time_, subStart_, subTime_));
	if (subP > 0.0f) {
		const int cs = static_cast<int>(clearTime_ * kCentisecondsPerSecondF + 0.5f);
		const int mm = (std::min)(cs / kCentisecondsPerMinute, kMaxDisplayMinutes);
		char buf[kTimeTextBufferSize];
		std::snprintf(buf, sizeof(buf), "CLEAR TIME  %02d' %02d\" %02d", mm, (cs / kCentisecondsPerSecond) % kSecondsPerMinute, cs % kCentisecondsPerSecond);
		const float w = tr->MeasureWidth(buf, subScale_);
		const float y = bandBottom + lineGap_ + kSubTextGap + (1.0f - subP) * kSubTextRise;
		tr->DrawText(buf, { (screenW - w) * 0.5f, y }, subScale_, { gold.x, gold.y, gold.z, subP }, kSubOutlineThickness, { 0.0f, 0.0f, 0.0f, subP * kSubOutlineAlpha });
	}
}

void StageClearBanner::OnImGui() {
#ifdef _DEBUG
	ImGui::SeparatorText("STAGE CLEAR Banner");
	ImGui::DragFloat("Center Y Ratio", &centerYRatio_, 0.005f, 0.1f, 0.9f, "%.3f");
	ImGui::DragFloat("Band Height", &bandHeight_, 1.0f, 20.0f, 400.0f, "%.0f");
	ImGui::DragFloat("Band Open Time", &bandOpenTime_, 0.01f, 0.0f, 2.0f, "%.2f");
	ImGui::DragFloat("Band Alpha", &bandAlpha_, 0.01f, 0.0f, 1.0f, "%.2f");
	ImGui::DragFloat("Line Start", &lineStart_, 0.01f, 0.0f, 2.0f, "%.2f");
	ImGui::DragFloat("Line Time", &lineTime_, 0.01f, 0.0f, 2.0f, "%.2f");
	ImGui::DragFloat("Line Width Ratio", &lineWidthRatio_, 0.01f, 0.1f, 1.0f, "%.2f");
	ImGui::DragFloat("Text Scale", &textScale_, 0.05f, 0.5f, 8.0f, "%.2f");
	ImGui::DragFloat("Text Start", &textStart_, 0.01f, 0.0f, 2.0f, "%.2f");
	ImGui::DragFloat("Text Time", &textTime_, 0.01f, 0.0f, 3.0f, "%.2f");
	ImGui::DragFloat("Spacing From", &textSpacingFrom_, 0.5f, 0.0f, 200.0f, "%.1f");
	ImGui::DragFloat("Spacing To", &textSpacingTo_, 0.5f, -20.0f, 100.0f, "%.1f");
	ImGui::DragFloat("Font Px", &textFontPx_, 0.5f, 8.0f, 128.0f, "%.1f");
	ImGui::DragFloat("Sweep Start", &sweepStart_, 0.01f, 0.0f, 3.0f, "%.2f");
	ImGui::DragFloat("Sweep Time", &sweepTime_, 0.01f, 0.05f, 3.0f, "%.2f");
	ImGui::DragFloat("Sweep Width", &sweepWidth_, 1.0f, 4.0f, 400.0f, "%.0f");
	ImGui::DragFloat("Sweep Alpha", &sweepAlpha_, 0.01f, 0.0f, 1.0f, "%.2f");
	ImGui::DragFloat("Sub Start", &subStart_, 0.01f, 0.0f, 3.0f, "%.2f");
	ImGui::DragFloat("Sub Scale", &subScale_, 0.05f, 0.3f, 4.0f, "%.2f");
	if (ImGui::Button("Replay Banner")) Start(clearTime_);
#endif
}
