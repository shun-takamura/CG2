#include "AttractMode.h"

#include "TextRenderer.h"

AttractMode* AttractMode::GetInstance() {
	static AttractMode instance;
	return &instance;
}

// 2026-11-10：Release の exe で起動時から始めるのはやめた（展示はタイトルの放置デモ＝TitleDemoMovie で動画を流す）。F8 で始めるのは残す
AttractMode::AttractMode() = default;

void AttractMode::SetEnabled(bool enabled) {
	// 始めるたびに、最初の周は一人称から
	if (enabled && !enabled_) {
		thirdPersonLap = false;
		lapDeaths = 0;
	}
	enabled_ = enabled;
}

void AttractMode::DrawLabel() const {
	if (!enabled_) return;
	TextRenderer* tr = TextRenderer::GetInstance();
	if (!tr->IsInitialized()) return;
	tr->DrawText("デモプレイ中　F8 で通常モードに戻る", { 28.0f, 24.0f }, 0.85f,
		{ 1.0f, 1.0f, 1.0f, 0.9f }, 2.0f, { 0.0f, 0.0f, 0.0f, 0.6f });
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
