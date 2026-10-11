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
#include "SoundManager.h"
#include "Vector4.h"
#include "AttractMode.h"
#include "UIPointer.h"
#include "SpriteInstance.h"
#include "SpriteManager.h"

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

	// 白い背景（墨のトランジションの黒が映えるように）。文字は白＋黒の縁取りなので白地でも読める
	background_ = std::make_unique<SpriteInstance>();
	background_->Initialize(spriteManager_, "Resources/Textures/white1x1.dds", "HubBackground");
	background_->SetPosition({ 0.0f, 0.0f });
	background_->SetSize({ static_cast<float>(dxCore_->GetSwapChainWidth()),
		static_cast<float>(dxCore_->GetSwapChainHeight()) });
	background_->SetColor({ 1.0f, 1.0f, 1.0f, 1.0f });
	background_->Update();
	SoundManager::GetInstance()->Play2DSoundLooped("bgm_title", 0.5f);
	attractTimer_ = 0.0f;
}

void HubScene::Finalize() {
	SoundManager::GetInstance()->Stop2DSound("bgm_title");
}

void HubScene::Update() {
	if (SceneManager::GetInstance()->IsTransitioning()) {
		return;
	}

	auto* actions = input_->GetActionMap();
	if (!actions) return;

	// デモモード：一定時間でカーソルを「タイトルに戻る」へ動かし、少し見せてから決定してタイトルへ
	if (auto* attract = AttractMode::GetInstance(); attract->IsEnabled()) {
		const float prev = attractTimer_;
		attractTimer_ += GetScaledDeltaTime();
		if (prev < attract->hubWait && attractTimer_ >= attract->hubWait) {
			menu_.SetSelectedIndex(kHubBackToTitle);
			SoundManager::GetInstance()->Play2DSound("se_ui_cursor");
		}
		if (attractTimer_ >= attract->hubWait + attract->hubConfirmDelay) {
			SoundManager::GetInstance()->Play2DSound("se_ui_decide");
			SceneManager::GetInstance()->ChangeScene("TITLE");
			return;
		}
	} else {
		attractTimer_ = 0.0f;
	}

	menu_.SetPosition({ static_cast<float>(dxCore_->GetSwapChainWidth()) * 0.5f,
		static_cast<float>(dxCore_->GetSwapChainHeight()) * 0.55f });
	const UIPointer pointer = UIPointer::FromInput(input_);
	const VerticalMenu::Result result = menu_.Update(actions, &pointer);
	if (result == VerticalMenu::Result::Canceled) {
		SceneManager::GetInstance()->ChangeScene("TITLE");
		return;
	}
	if (result == VerticalMenu::Result::Confirmed) {
		switch (menu_.GetSelectedIndex()) {
		case kHubStage1:
			SceneManager::GetInstance()->ChangeScene("STAGEPLAY");
			return;
		case kHubBackToTitle:
			SceneManager::GetInstance()->ChangeScene("TITLE");
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

	if (background_) {
		spriteManager_->DrawSetting();
		background_->Draw();
	}

	const char* heading = "STAGE SELECT";
	const float headingScale = 2.0f;
	const float headingW = tr->MeasureWidth(heading, headingScale);
	tr->DrawText(heading, { (screenW - headingW) * 0.5f, screenH * 0.25f }, headingScale,
		{ 1.0f, 1.0f, 1.0f, 1.0f }, 3.0f, { 0.0f, 0.0f, 0.0f, 1.0f });

	menu_.SetPosition({ screenW * 0.5f, screenH * 0.55f });
	menu_.Draw();
	tr->Flush();
}

Camera* HubScene::GetCamera() {
	return camera_.get();
}
