#include "GameOverScene.h"

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
#include "Score/BossRetryState.h"
#include "Vector4.h"
#include <string>

GameOverScene::GameOverScene() = default;
GameOverScene::~GameOverScene() = default;

void GameOverScene::Initialize() {
	Game::GetPostEffect()->ResetEffects();

	camera_ = std::make_unique<Camera>();
	camera_->SetTranslate({ 0.0f, 0.0f, -10.0f });
	camera_->SetRotate({ 0.0f, 0.0f, 0.0f });
	object3DManager_->SetDefaultCamera(camera_.get());

	options_.clear();
	if (BossRetryState::GetInstance()->HasCheckpoint()) {
		options_.push_back(Option::BossRetry);
	}
	options_.push_back(Option::RailRetry);
	options_.push_back(Option::BackToStageSelect);
	selectedIndex_ = 0;
}

void GameOverScene::Finalize() {}

void GameOverScene::Update() {
	if (SceneManager::GetInstance()->IsTransitioning()) {
		return;
	}
	if (options_.empty()) return;

	auto* actions = input_->GetActionMap();
	if (!actions) return;

	if (actions->IsTriggered(static_cast<int>(Action::MenuDown))) {
		selectedIndex_ = (selectedIndex_ + 1) % static_cast<int>(options_.size());
	}
	if (actions->IsTriggered(static_cast<int>(Action::MenuUp))) {
		selectedIndex_ = (selectedIndex_ + static_cast<int>(options_.size()) - 1) % static_cast<int>(options_.size());
	}

	if (actions->IsTriggered(static_cast<int>(Action::MenuConfirm))) {
		switch (options_[selectedIndex_]) {
		case Option::BossRetry:
			BossRetryState::GetInstance()->RequestBossRetry();
			SceneManager::GetInstance()->ChangeScene("STAGEPLAY", TransitionType::Fade);
			return;
		case Option::RailRetry:
			SceneManager::GetInstance()->ChangeScene("STAGEPLAY", TransitionType::Fade);
			return;
		case Option::BackToStageSelect:
			SceneManager::GetInstance()->ChangeScene("STAGESELECT", TransitionType::Fade);
			return;
		}
	}

	camera_->Update();
}

namespace {
	const char* OptionLabel(GameOverScene::Option opt) {
		switch (opt) {
		case GameOverScene::Option::BossRetry:          return "ボス戦からリトライ";
		case GameOverScene::Option::RailRetry:          return "シューティングからリトライ";
		case GameOverScene::Option::BackToStageSelect:  return "ステージセレクトに戻る";
		}
		return "";
	}
}

void GameOverScene::Draw() {
	TextRenderer* tr = TextRenderer::GetInstance();
	if (!tr->IsInitialized()) return;

	const float scale = 1.5f;
	const float lineHeight = 60.0f;
	const float screenW = static_cast<float>(dxCore_->GetSwapChainWidth());
	const float screenH = static_cast<float>(dxCore_->GetSwapChainHeight());

	const float totalHeight = lineHeight * static_cast<float>(options_.size());
	float y = (screenH - totalHeight) * 0.5f;

	for (size_t i = 0; i < options_.size(); ++i) {
		const bool selected = (static_cast<int>(i) == selectedIndex_);
		const Vector4 color = selected
			? Vector4{ 1.0f, 0.85f, 0.2f, 1.0f }
			: Vector4{ 1.0f, 1.0f, 1.0f, 1.0f };
		std::string label = (selected ? "> " : "  ") + std::string(OptionLabel(options_[i]));
		const float w = tr->MeasureWidth(label, scale);
		tr->DrawText(label, { (screenW - w) * 0.5f, y + lineHeight * static_cast<float>(i) }, scale,
			color, 2.0f, { 0.0f, 0.0f, 0.0f, 1.0f });
	}
	tr->Flush();
}

Camera* GameOverScene::GetCamera() {
	return camera_.get();
}
