#include "BaseTransition.h"
#include "DirectXCore.h"
#include <algorithm>

void BaseTransition::Start(std::function<void()> onSceneChange, bool playReveal) {
	if (isTransitioning_) return;

	isTransitioning_ = true;
	state_ = TransitionState::FadeIn;
	currentTime_ = 0.0f;
	holdTime_ = 0.0f;
	sceneChangeTriggered_ = false;
	onSceneChange_ = onSceneChange;
	playReveal_ = playReveal;
	OnStartCover();
}

void BaseTransition::StartReveal() {
	isTransitioning_ = true;
	state_ = TransitionState::FadeOut;
	currentTime_ = 0.0f;
	sceneChangeTriggered_ = true;
	onSceneChange_ = nullptr;
	playReveal_ = true;
	SetFullyCovered();
	UpdateVisual();
}

void BaseTransition::Update() {
	if (isTransitioning_) {
		const float dt = GetStepDeltaTime();
		currentTime_ += dt;

		switch (state_) {
		case TransitionState::FadeIn:
			if (UpdateCover(dt)) {
				state_ = TransitionState::Hold;
				currentTime_ = 0.0f;
				holdTime_ = 0.0f;
			}
			break;

		case TransitionState::Hold:
			// 覆い切った絵を1フレーム出した次のフレームでシーンを差し替える。
			// 差し替え（ロード）は同期なので、戻ってきた時点でロード完了。Hold の秒数はここから数える
			if (!sceneChangeTriggered_) {
				sceneChangeTriggered_ = true;
				if (onSceneChange_) onSceneChange_();
				break;
			}
			holdTime_ += dt;
			if (holdTime_ >= holdDuration_) {
				if (playReveal_) {
					state_ = TransitionState::FadeOut;
					currentTime_ = 0.0f;
				} else {
					Finish();
				}
			}
			break;

		case TransitionState::FadeOut:
			if (UpdateReveal(dt)) {
				Finish();
			}
			break;

		default:
			break;
		}
	}

	UpdateVisual();
}

float BaseTransition::GetStepDeltaTime() const {
	const float dt = dxCore_ ? dxCore_->GetDeltaTime() : (1.0f / 60.0f);
	return (std::min)(dt, kMaxStepDeltaTime);
}

void BaseTransition::Finish() {
	state_ = TransitionState::None;
	isTransitioning_ = false;
	OnFinished();
}
