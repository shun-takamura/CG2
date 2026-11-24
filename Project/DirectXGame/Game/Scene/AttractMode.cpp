#include "AttractMode.h"

#include "TextRenderer.h"

namespace {
	// 「デモプレイ中」の表示位置 X（画面の左上から）[px]
	constexpr float kDemoLabelPosX = 28.0f;
	// 「デモプレイ中」の表示位置 Y（画面の左上から）[px]
	constexpr float kDemoLabelPosY = 24.0f;
	// 「デモプレイ中」の文字の大きさの倍率
	constexpr float kDemoLabelScale = 0.85f;
	// 「デモプレイ中」の文字の不透明度
	constexpr float kDemoLabelAlpha = 0.9f;
	// 「デモプレイ中」の文字のアウトラインの太さ [px]
	constexpr float kDemoLabelOutlineThickness = 2.0f;
	// 「デモプレイ中」の文字のアウトラインの不透明度
	constexpr float kDemoLabelOutlineAlpha = 0.6f;

	// 展示ロック・全能モードの案内の右端の余白（画面の右上から）[px]
	constexpr float kExhibitionLabelMarginX = 28.0f;
	// 展示ロック・全能モードの案内の 1 行目の位置 Y（画面の上から）[px]
	constexpr float kExhibitionLabelPosY = 24.0f;
	// 展示ロック・全能モードの案内の行の間隔 [px]
	constexpr float kExhibitionLabelLineHeight = 34.0f;
	// 展示ロック・全能モードの案内の文字の大きさの倍率
	constexpr float kExhibitionLabelScale = 0.75f;
	// 展示ロック・全能モードの案内の文字の不透明度
	constexpr float kExhibitionLabelAlpha = 0.85f;
	// 全能モードの案内の文字色（金）の G・B
	constexpr float kShowcaseLabelColorG = 0.85f;
	constexpr float kShowcaseLabelColorB = 0.4f;
	constexpr const char* kExhibitionLockLabel = "展示モード中　F8 で解除";
	constexpr const char* kShowcaseLabel = "全能モード　F9 で切り替え（タイトル）";
}

AttractMode* AttractMode::GetInstance() {
	static AttractMode instance;
	return &instance;
}

// 2026-11-10：Release の exe で起動時から始めるのはやめた（展示はタイトルの放置デモ＝TitleDemoMovie で動画を流す）。
// 2026-11-24：Release の exe は展示ロック中で起動する（タイトルに戻ってから映像が流れるまでに来場者が操作してしまうため）。
// Development も NDEBUG なので、USE_PEPPER が無いことで Release を見分ける
AttractMode::AttractMode() {
#if defined(NDEBUG) && !defined(USE_PEPPER)
	exhibitionLocked_ = true;
#endif
}

void AttractMode::DrawExhibitionLabels(float screenWidth) const {
	if (!exhibitionLocked_ && !showcase_) return;
	TextRenderer* tr = TextRenderer::GetInstance();
	if (!tr->IsInitialized()) return;

	float y = kExhibitionLabelPosY;
	// 右寄せで 1 行描いて、次の行へ
	auto drawLine = [&](const char* text, const Vector4& color) {
		const float w = tr->MeasureWidth(text, kExhibitionLabelScale);
		tr->DrawText(text, { screenWidth - kExhibitionLabelMarginX - w, y }, kExhibitionLabelScale,
			color, kDemoLabelOutlineThickness, { 0.0f, 0.0f, 0.0f, kDemoLabelOutlineAlpha });
		y += kExhibitionLabelLineHeight;
	};
	if (exhibitionLocked_) drawLine(kExhibitionLockLabel, { 1.0f, 1.0f, 1.0f, kExhibitionLabelAlpha });
	if (showcase_) drawLine(kShowcaseLabel, { 1.0f, kShowcaseLabelColorG, kShowcaseLabelColorB, kExhibitionLabelAlpha });
	tr->Flush();
}

void AttractMode::SetEnabled(bool enabled) {
	// 始めるたびに、最初の周は一人称から
	if (enabled && !enabled_) {
		thirdPersonLap_ = false;
		lapDeaths_ = 0;
	}
	enabled_ = enabled;
}

void AttractMode::DrawLabel() const {
	if (!enabled_) return;
	TextRenderer* tr = TextRenderer::GetInstance();
	if (!tr->IsInitialized()) return;
	tr->DrawText("デモプレイ中　F8 で通常モードに戻る", { kDemoLabelPosX, kDemoLabelPosY }, kDemoLabelScale,
		{ 1.0f, 1.0f, 1.0f, kDemoLabelAlpha }, kDemoLabelOutlineThickness, { 0.0f, 0.0f, 0.0f, kDemoLabelOutlineAlpha });
	tr->Flush();
}

AttractMenuTimer::Step AttractMenuTimer::Advance(float deltaTime, float wait, float confirmDelay) {
	time_ += deltaTime;
	if (time_ >= wait + confirmDelay) {
		moved_ = true;
		return Step::Confirm;
	}
	if (!moved_ && time_ >= wait) {
		moved_ = true;
		return Step::MoveCursor;
	}
	return Step::Wait;
}
