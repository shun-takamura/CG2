#include "ResultScene.h"

#include "Camera.h"
#include "Object3DManager.h"
#include "SceneManager.h"
#include "TransitionManager.h"
#include "InputManager.h"
#include "KeyboardInput.h"
#include "Game.h"

ResultScene::ResultScene() = default;
ResultScene::~ResultScene() = default;

void ResultScene::Initialize() {
	Game::GetPostEffect()->ResetEffects();

	camera_ = std::make_unique<Camera>();
	camera_->SetTranslate({ 0.0f, 0.0f, -10.0f });
	camera_->SetRotate({ 0.0f, 0.0f, 0.0f });
	object3DManager_->SetDefaultCamera(camera_.get());

	score_ = 0;
	gainedSP_ = 0;
}

void ResultScene::Finalize() {}

void ResultScene::Update() {
	if (SceneManager::GetInstance()->IsTransitioning()) {
		return;
	}

	// SPACE/Enter で Hub へ戻る
	auto* kb = input_->GetKeyboard();
	if (kb->TriggerKey(DIK_SPACE) || kb->TriggerKey(DIK_RETURN)) {
		SceneManager::GetInstance()->ChangeScene("HUB", TransitionType::Fade);
		return;
	}

	camera_->Update();
}

void ResultScene::Draw() {
	// スコア・SP表示は後で追加
}

Camera* ResultScene::GetCamera() {
	return camera_.get();
}
