#include "StageClearBanner.h"

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
	float Clamp01(float x) { return (std::clamp)(x, 0.0f, 1.0f); }
	float SmoothStep01(float x) { x = Clamp01(x); return x * x * (3.0f - 2.0f * x); }
	float EaseOutCubic(float x) { x = Clamp01(x); const float u = 1.0f - x; return 1.0f - u * u * u; }
	// 区間 [start, start+len] での進み具合（0..1）
	float Phase(float t, float start, float len) { return len <= 1e-4f ? (t >= start ? 1.0f : 0.0f) : Clamp01((t - start) / len); }
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
		tr->DrawRect({ (screenW - w) * 0.5f, bandTop }, { w, bandHeight_ }, { 0.02f, 0.03f, 0.06f, bandAlpha_ * bandOpen });
	}

	// ----- 金の線：上下の縁を、上は右へ・下は左へ走る -----
	const Vector4 gold{ 1.0f, 0.82f, 0.42f, 1.0f };
	const float lineP = EaseOutCubic(Phase(time_, lineStart_, lineTime_));
	if (lineP > 0.0f) {
		const float full = screenW * lineWidthRatio_;
		const float left = (screenW - full) * 0.5f;
		const float len = full * lineP;
		tr->DrawRect({ left, bandTop - lineGap_ - 2.0f }, { len, 2.0f }, gold);
		tr->DrawRect({ left + full - len, bandBottom + lineGap_ }, { len, 2.0f }, gold);
		// 線の先の小さな菱形の代わりの点
		tr->DrawRect({ left + len - 3.0f, bandTop - lineGap_ - 4.0f }, { 6.0f, 6.0f }, gold);
		tr->DrawRect({ left + full - len - 3.0f, bandBottom + lineGap_ - 2.0f }, { 6.0f, 6.0f }, gold);
	}

	// ----- 文字：字間が詰まりながら出る。詰まりきる所で一瞬白く -----
	constexpr const char* kText = "STAGE CLEAR";
	const float textP = Phase(time_, textStart_, textTime_);
	if (textP > 0.0f) {
		const float spacing = textSpacingFrom_ + (textSpacingTo_ - textSpacingFrom_) * EaseOutCubic(textP);
		const float alpha = SmoothStep01(textP * 1.6f);
		// 詰まりきった直後（textP=1 から 0.25 秒）に白へ寄せて戻す
		const float landT = time_ - (textStart_ + textTime_);
		const float flash = (landT >= 0.0f && landT < 0.25f) ? (1.0f - landT / 0.25f) : 0.0f;
		const Vector4 base{ 1.0f, 0.94f, 0.76f, alpha };
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
			if (ch != " ") tr->DrawText(ch, { x, y }, textScale_, color, 2.5f, { 0.0f, 0.0f, 0.0f, alpha * 0.85f });
			x += tr->MeasureWidth(ch, textScale_) + spacing;
		}
	}

	// ----- 光の筋：帯の中を左から右へ（文字の上に薄く重ねる。発光の層は使わない）-----
	const float sweepP = Phase(time_, sweepStart_, sweepTime_);
	if (sweepP > 0.0f && sweepP < 1.0f) {
		const float full = screenW * lineWidthRatio_;
		const float left = (screenW - full) * 0.5f - sweepWidth_;
		const float x = left + (full + sweepWidth_) * SmoothStep01(sweepP);
		const float fade = std::sin(sweepP * 3.14159265f);
		// 芯と、その両側の薄い縁
		tr->DrawRect({ x - sweepWidth_ * 0.5f, bandTop }, { sweepWidth_, bandHeight_ }, { 1.0f, 0.97f, 0.88f, sweepAlpha_ * 0.4f * fade });
		tr->DrawRect({ x - sweepWidth_ * 0.25f, bandTop }, { sweepWidth_ * 0.5f, bandHeight_ }, { 1.0f, 0.97f, 0.88f, sweepAlpha_ * 0.6f * fade });
		tr->DrawRect({ x - sweepWidth_ * 0.08f, bandTop }, { sweepWidth_ * 0.16f, bandHeight_ }, { 1.0f, 1.0f, 1.0f, sweepAlpha_ * fade });
	}

	// ----- クリアタイム（帯の下の線のさらに下）-----
	const float subP = SmoothStep01(Phase(time_, subStart_, subTime_));
	if (subP > 0.0f) {
		const int cs = static_cast<int>(clearTime_ * 100.0f + 0.5f);
		const int mm = (std::min)(cs / 6000, 99);
		char buf[48];
		std::snprintf(buf, sizeof(buf), "CLEAR TIME  %02d' %02d\" %02d", mm, (cs / 100) % 60, cs % 100);
		const float w = tr->MeasureWidth(buf, subScale_);
		const float y = bandBottom + lineGap_ + 16.0f + (1.0f - subP) * 8.0f;
		tr->DrawText(buf, { (screenW - w) * 0.5f, y }, subScale_, { gold.x, gold.y, gold.z, subP }, 1.5f, { 0.0f, 0.0f, 0.0f, subP * 0.8f });
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
