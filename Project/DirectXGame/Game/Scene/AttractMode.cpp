#include "AttractMode.h"

#include "TextRenderer.h"

AttractMode* AttractMode::GetInstance() {
	static AttractMode instance;
	return &instance;
}

void AttractMode::SetEnabled(bool enabled) {
	// 始めるたびに、ステージ1 の 1 回目はゲームオーバーから
	if (enabled && !enabled_) nextStageRun = StageRun::GameOver;
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
