#include "HubScene.h"

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
#include "Vector4.h"

namespace {
	enum HubItem : int {
		kHubStage1 = 0,
		kHubBackToTitle,
	};
}

HubScene::HubScene() = default;
HubScene::~HubScene() = default;

void HubScene::Initialize() {
	Game::GetPostEffect()->ResetEffects();

	camera_ = std::make_unique<Camera>();
	camera_->SetTranslate({ 0.0f, 0.0f, -10.0f });
	camera_->SetRotate({ 0.0f, 0.0f, 0.0f });
	object3DManager_->SetDefaultCamera(camera_.get());

	menu_.SetItems({ "Stage1", "タイトルに戻る" });
}

void HubScene::Finalize() {}

void HubScene::Update() {
	if (SceneManager::GetInstance()->IsTransitioning()) {
		return;
	}

	auto* actions = input_->GetActionMap();
	if (!actions) return;

	const VerticalMenu::Result result = menu_.Update(actions);
	if (result == VerticalMenu::Result::Canceled) {
		SceneManager::GetInstance()->ChangeScene("TITLE", TransitionType::Stripe);
		return;
	}
	if (result == VerticalMenu::Result::Confirmed) {
		switch (menu_.GetSelectedIndex()) {
		case kHubStage1:
			SceneManager::GetInstance()->ChangeScene("STAGEPLAY", TransitionType::Stripe);
			return;
		case kHubBackToTitle:
			SceneManager::GetInstance()->ChangeScene("TITLE", TransitionType::Stripe);
			return;
		}
	}

	camera_->Update();
}

void HubScene::Draw() {
	TextRenderer* tr = TextRenderer::GetInstance();
	if (!tr->IsInitialized()) return;

	const float screenW = static_cast<float>(dxCore_->GetSwapChainWidth());
	const float screenH = static_cast<float>(dxCore_->GetSwapChainHeight());

	const char* heading = "STAGE SELECT";
	const float headingScale = 2.0f;
	const float headingW = tr->MeasureWidth(heading, headingScale);
	tr->DrawText(heading, { (screenW - headingW) * 0.5f, screenH * 0.25f }, headingScale,
		{ 1.0f, 1.0f, 1.0f, 1.0f }, 3.0f, { 0.0f, 0.0f, 0.0f, 1.0f });

	menu_.Draw({ screenW * 0.5f, screenH * 0.55f });
	tr->Flush();
}

Camera* HubScene::GetCamera() {
	return camera_.get();
}
