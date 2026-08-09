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
#include <string>

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

	// 上下で選択切替
	if (actions->IsTriggered(static_cast<int>(Action::MenuDown))) {
		int next = (static_cast<int>(currentTab_) + 1) % 2;
		currentTab_ = static_cast<Tab>(next);
	}
	if (actions->IsTriggered(static_cast<int>(Action::MenuUp))) {
		int next = (static_cast<int>(currentTab_) + 1) % 2; // 2択のため up/down は同じ挙動
		currentTab_ = static_cast<Tab>(next);
	}

	// 決定で現在選択の動作を実行
	if (actions->IsTriggered(static_cast<int>(Action::MenuConfirm))) {
		switch (currentTab_) {
		case Tab::StageSelect:
			SceneManager::GetInstance()->ChangeScene("STAGESELECT", TransitionType::Fade);
			return;
		case Tab::Quit:
			PostQuitMessage(0);
			return;
		}
	}

	camera_->Update();
}

namespace {
	const char* HubTabLabel(HubScene::Tab tab) {
		switch (tab) {
		case HubScene::Tab::StageSelect: return "StageSelect";
		case HubScene::Tab::Quit:        return "ゲーム終了";
		}
		return "";
	}
}

void HubScene::Draw() {
	TextRenderer* tr = TextRenderer::GetInstance();
	if (!tr->IsInitialized()) return;

	const float scale = 1.5f;
	const float lineHeight = 60.0f;
	const float screenW = static_cast<float>(dxCore_->GetSwapChainWidth());
	const float screenH = static_cast<float>(dxCore_->GetSwapChainHeight());

	const Tab tabs[2] = { Tab::StageSelect, Tab::Quit };
	const float totalHeight = lineHeight * 2.0f;
	float y = (screenH - totalHeight) * 0.5f;

	for (int i = 0; i < 2; ++i) {
		const bool selected = (tabs[i] == currentTab_);
		const Vector4 color = selected
			? Vector4{ 1.0f, 0.85f, 0.2f, 1.0f }
			: Vector4{ 1.0f, 1.0f, 1.0f, 1.0f };
		std::string label = (selected ? "> " : "  ") + std::string(HubTabLabel(tabs[i]));
		const float w = tr->MeasureWidth(label, scale);
		tr->DrawText(label, { (screenW - w) * 0.5f, y + lineHeight * static_cast<float>(i) }, scale,
			color, 2.0f, { 0.0f, 0.0f, 0.0f, 1.0f });
	}
	tr->Flush();
}

Camera* HubScene::GetCamera() {
	return camera_.get();
}
