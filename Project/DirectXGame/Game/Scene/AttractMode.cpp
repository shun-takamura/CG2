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
}

AttractMode* AttractMode::GetInstance() {
	static AttractMode instance;
	return &instance;
}

// 2026-11-10：Release の exe で起動時から始めるのはやめた（展示はタイトルの放置デモ＝TitleDemoMovie で動画を流す）。F8 で始めるのは残す
AttractMode::AttractMode() = default;

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
