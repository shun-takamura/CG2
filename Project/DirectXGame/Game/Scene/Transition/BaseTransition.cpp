#include "BaseTransition.h"

void BaseTransition::Start(std::function<void()> onSceneChange) {
	if (isTransitioning_) return;

	isTransitioning_ = true;
	state_ = TransitionState::FadeIn;
	currentTime_ = 0.0f;
	sceneChangeTriggered_ = false;
	onSceneChange_ = onSceneChange;
}

void BaseTransition::AdvanceState() {
	switch (state_) {
	case TransitionState::FadeIn:
		state_ = TransitionState::Hold;
		currentTime_ = 0.0f;
		break;

	case TransitionState::Hold:
		// シーン切り替えを実行
		if (!sceneChangeTriggered_ && onSceneChange_) {
			onSceneChange_();
			sceneChangeTriggered_ = true;
		}
		if (currentTime_ >= holdDuration_) {
			state_ = TransitionState::FadeOut;
			currentTime_ = 0.0f;
		}
		break;

	case TransitionState::FadeOut:
		state_ = TransitionState::None;
		isTransitioning_ = false;
		break;

	default:
		break;
	}
}
