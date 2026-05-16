#include "HubScene.h"

#include "Camera.h"
#include "Object3DManager.h"
#include "SceneManager.h"
#include "TransitionManager.h"
#include "InputManager.h"
#include "InputAction.h"
#include "Config/GameActions.h"
#include "Game.h"

HubScene::HubScene() = default;
HubScene::~HubScene() = default;

void HubScene::Initialize() {
	Game::GetPostEffect()->ResetEffects();

	camera_ = std::make_unique<Camera>();
	camera_->SetTranslate({ 0.0f, 0.0f, -10.0f });
	camera_->SetRotate({ 0.0f, 0.0f, 0.0f });
	object3DManager_->SetDefaultCamera(camera_.get());

	currentTab_ = Tab::StageSelect;
}

void HubScene::Finalize() {}

void HubScene::Update() {
	if (SceneManager::GetInstance()->IsTransitioning()) {
		return;
	}

	auto* actions = input_->GetActionMap();
	if (!actions) return;

	// 左右でタブ切替
	if (actions->IsTriggered(static_cast<int>(Action::MenuRight))) {
		int next = (static_cast<int>(currentTab_) + 1) % 5;
		currentTab_ = static_cast<Tab>(next);
	}
	if (actions->IsTriggered(static_cast<int>(Action::MenuLeft))) {
		int next = (static_cast<int>(currentTab_) + 4) % 5;
		currentTab_ = static_cast<Tab>(next);
	}

	// 決定で現在タブの動作を実行
	if (actions->IsTriggered(static_cast<int>(Action::MenuConfirm))) {
		switch (currentTab_) {
		case Tab::StageSelect:
			SceneManager::GetInstance()->ChangeScene("STAGEPLAY", TransitionType::Fade);
			return;
		case Tab::SkillShop:
		case Tab::SkillEquip:
			// 仮実装。サブUI起動予定
			break;
		case Tab::BackToTitle:
			SceneManager::GetInstance()->ChangeScene("TITLE", TransitionType::Fade);
			return;
		case Tab::Quit:
			PostQuitMessage(0);
			return;
		}
	}

	camera_->Update();
}

void HubScene::Draw() {
	// タブUI描画は後で追加
}

Camera* HubScene::GetCamera() {
	return camera_.get();
}
