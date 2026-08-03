#include "ResultScene.h"

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
#include "Score/ScoreManager.h"
#include <cstdio>

ResultScene::ResultScene() = default;
ResultScene::~ResultScene() = default;

void ResultScene::Initialize() {
	Game::GetPostEffect()->ResetEffects();

	camera_ = std::make_unique<Camera>();
	camera_->SetTranslate({ 0.0f, 0.0f, -10.0f });
	camera_->SetRotate({ 0.0f, 0.0f, 0.0f });
	object3DManager_->SetDefaultCamera(camera_.get());

	score_ = ScoreManager::GetInstance()->GetScore();
	killCount_ = ScoreManager::GetInstance()->GetKillCount();
}

void ResultScene::Finalize() {}

void ResultScene::Update() {
	if (SceneManager::GetInstance()->IsTransitioning()) {
		return;
	}

	auto* actions = input_->GetActionMap();
	if (actions && actions->IsTriggered(static_cast<int>(Action::MenuConfirm))) {
		SceneManager::GetInstance()->ChangeScene("STAGESELECT", TransitionType::Fade);
		return;
	}

	camera_->Update();
}

void ResultScene::Draw() {
	TextRenderer* tr = TextRenderer::GetInstance();
	if (!tr->IsInitialized()) return;

	const float scale = 1.5f;
	const float lineHeight = 60.0f;
	const float screenW = static_cast<float>(dxCore_->GetSwapChainWidth());
	const float screenH = static_cast<float>(dxCore_->GetSwapChainHeight());

	char scoreBuf[32];
	std::snprintf(scoreBuf, sizeof(scoreBuf), "SCORE: %d", score_);
	char killBuf[32];
	std::snprintf(killBuf, sizeof(killBuf), "撃破数: %d", killCount_);

	const float scoreW = tr->MeasureWidth(scoreBuf, scale);
	const float killW = tr->MeasureWidth(killBuf, scale);
	const float y = (screenH - lineHeight * 2.0f) * 0.5f;

	tr->DrawText(scoreBuf, { (screenW - scoreW) * 0.5f, y }, scale,
		{ 1.0f, 1.0f, 1.0f, 1.0f }, 2.0f, { 0.0f, 0.0f, 0.0f, 1.0f });
	tr->DrawText(killBuf, { (screenW - killW) * 0.5f, y + lineHeight }, scale,
		{ 1.0f, 1.0f, 1.0f, 1.0f }, 2.0f, { 0.0f, 0.0f, 0.0f, 1.0f });
	tr->Flush();
}

Camera* ResultScene::GetCamera() {
	return camera_.get();
}
