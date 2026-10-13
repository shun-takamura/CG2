#include "StageSelectScene.h"

#include "Camera.h"
#include "Object3DManager.h"
#include "SceneManager.h"
#include "TransitionManager.h"
#include "InputManager.h"
#include "InputAction.h"
#include "Config/GameActions.h"
#include "Game.h"
#include "DirectXCore.h"
#include "TextRenderer.h"
#include "UIPointer.h"

namespace {
	enum StageSelectItem : int {
		kItemTutorial = 0,
		kItemStage1,
		kItemBackToHub,
	};
}

StageSelectScene::StageSelectScene() = default;
StageSelectScene::~StageSelectScene() = default;

void StageSelectScene::Initialize() {
	Game::GetPostEffect()->ResetEffects();

	camera_ = std::make_unique<Camera>();
	camera_->SetTranslate({ 0.0f, 0.0f, -10.0f });
	camera_->SetRotate({ 0.0f, 0.0f, 0.0f });
	object3DManager_->SetDefaultCamera(camera_.get());

	menu_.SetItems({ "チュートリアル", "ステージ1", "HUB に戻る" }, kItemStage1);
	// チュートリアルのステージができるまでは表示だけ（7_Tutorial.md）
	menu_.SetItemEnabled(kItemTutorial, false);
}

void StageSelectScene::Finalize() {}

void StageSelectScene::Update() {
	// 覆われている間だけ止める。開いている間は UI の起動を演じるので動かす（18 番 ①の IN）
	if (SceneManager::GetInstance()->IsTransitionCovering()) {
		return;
	}

	// UI の起動・シャットダウンの演出の更新はここ（入力を読む前）に置く
	camera_->Update();

	// 開いている途中は入力を読まない（遷移中の ChangeScene は無視されて、決定音だけ鳴ってしまうため）
	if (SceneManager::GetInstance()->IsTransitioning()) {
		return;
	}

	auto* actions = input_->GetActionMap();
	if (!actions) return;

	menu_.SetPosition({ static_cast<float>(dxCore_->GetSwapChainWidth()) * 0.5f,
		static_cast<float>(dxCore_->GetSwapChainHeight()) * 0.55f });
	const UIPointer pointer = UIPointer::FromInput(input_);
	const VerticalMenu::Result result = menu_.Update(actions, &pointer);
	if (result == VerticalMenu::Result::Canceled) {
		SceneManager::GetInstance()->ChangeScene("HUB");
		return;
	}
	if (result == VerticalMenu::Result::Confirmed) {
		switch (menu_.GetSelectedIndex()) {
		case kItemStage1:
			SceneManager::GetInstance()->ChangeScene("STAGEPLAY");
			return;
		case kItemBackToHub:
			// UI のシャットダウン（18 番 ②の OUT）はチャット C で演じてから切り替える形にする
			SceneManager::GetInstance()->ChangeScene("HUB");
			return;
		}
	}
}

void StageSelectScene::Draw() {
	TextRenderer* tr = TextRenderer::GetInstance();
	if (!tr->IsInitialized()) return;

	const float screenW = static_cast<float>(dxCore_->GetSwapChainWidth());
	const float screenH = static_cast<float>(dxCore_->GetSwapChainHeight());

	const char* heading = "STAGE SELECT";
	const float headingScale = 2.0f;
	const float headingW = tr->MeasureWidth(heading, headingScale);
	tr->DrawText(heading, { (screenW - headingW) * 0.5f, screenH * 0.25f }, headingScale,
		{ 1.0f, 1.0f, 1.0f, 1.0f }, 3.0f, { 0.0f, 0.0f, 0.0f, 1.0f });

	menu_.SetPosition({ screenW * 0.5f, screenH * 0.55f });
	menu_.Draw();
	tr->Flush();
}

Camera* StageSelectScene::GetCamera() {
	return camera_.get();
}
