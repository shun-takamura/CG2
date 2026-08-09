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

StageSelectScene::StageSelectScene() = default;
StageSelectScene::~StageSelectScene() = default;

void StageSelectScene::Initialize() {
	Game::GetPostEffect()->ResetEffects();

	camera_ = std::make_unique<Camera>();
	camera_->SetTranslate({ 0.0f, 0.0f, -10.0f });
	camera_->SetRotate({ 0.0f, 0.0f, 0.0f });
	object3DManager_->SetDefaultCamera(camera_.get());
}

void StageSelectScene::Finalize() {}

void StageSelectScene::Update() {
	if (SceneManager::GetInstance()->IsTransitioning()) {
		return;
	}

	auto* actions = input_->GetActionMap();
	if (actions && actions->IsTriggered(static_cast<int>(Action::MenuConfirm))) {
		SceneManager::GetInstance()->ChangeScene("STAGEPLAY", TransitionType::Fade);
		return;
	}

	camera_->Update();
}

void StageSelectScene::Draw() {
	TextRenderer* tr = TextRenderer::GetInstance();
	if (!tr->IsInitialized()) return;

	const char* label = "Stage1";
	const float scale = 2.0f;
	const float screenW = static_cast<float>(dxCore_->GetSwapChainWidth());
	const float screenH = static_cast<float>(dxCore_->GetSwapChainHeight());
	const float labelW = tr->MeasureWidth(label, scale);

	tr->DrawText(label, { (screenW - labelW) * 0.5f, screenH * 0.45f }, scale,
		{ 1.0f, 1.0f, 1.0f, 1.0f }, 2.0f, { 0.0f, 0.0f, 0.0f, 1.0f });
	tr->Flush();
}

Camera* StageSelectScene::GetCamera() {
	return camera_.get();
}
