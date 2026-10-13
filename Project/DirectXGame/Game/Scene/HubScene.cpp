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
#include "FadeTransition.h"
#include "Json/JsonParser.h"
#include "Json/JsonWriter.h"
#include <filesystem>

#ifdef _DEBUG
#include "imgui.h"
#endif

namespace {
	enum HubItem : int {
		kHubSortie = 0,   // 出撃（必殺技の切り替え・タイトルに戻るは後で足す。17 番 §0）
	};

	constexpr const char* kTuningPath = "Resources/Json/Tuning/Hub.json";
}

HubScene::HubScene() = default;
HubScene::~HubScene() = default;

void HubScene::Initialize() {
	Game::GetPostEffect()->ResetEffects();

	camera_ = std::make_unique<Camera>();
	camera_->SetTranslate({ 0.0f, 0.0f, -10.0f });
	camera_->SetRotate({ 0.0f, 0.0f, 0.0f });
	object3DManager_->SetDefaultCamera(camera_.get());

	LoadTuning();

	menu_.SetItems({ "出撃" });

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

	// デモモード：一定時間 HUB を見せてからタイトルへ戻る（メニューに「タイトルに戻る」が無いのでキャンセル扱い）
	if (auto* attract = AttractMode::GetInstance(); attract->IsEnabled()) {
		attractTimer_ += GetScaledDeltaTime();
		if (attractTimer_ >= attract->hubWait + attract->hubConfirmDelay) {
			SoundManager::GetInstance()->Play2DSound("se_ui_cancel");
			ChangeToTitle();
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
		ChangeToTitle();
		return;
	}
	if (result == VerticalMenu::Result::Confirmed) {
		switch (menu_.GetSelectedIndex()) {
		case kHubSortie:
			// 乗り込み（18 番 ①の OUT）はチャット B で HUB が演じてから切り替える形にする
			SceneManager::GetInstance()->ChangeScene("STAGESELECT");
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

	const char* heading = "HUB";
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

void HubScene::ChangeToTitle() {
	// タイトル ↔ HUB は白フェード（18 番 ⑧）。Fade の既定は黒なので、色と長さを直前に決める
	if (auto* fade = TransitionManager::GetInstance()->GetTransition<FadeTransition>(TransitionType::Fade)) {
		fade->SetNextFade({ 1.0f, 1.0f, 1.0f }, whiteFadeSeconds_, whiteHoldSeconds_);
	}
	SceneManager::GetInstance()->ChangeScene("TITLE");
}

void HubScene::LoadTuning() {
	if (!std::filesystem::exists(kTuningPath)) return;
	const auto result = JsonParser::ParseFile(kTuningPath);
	if (!result.success) return;
	const JsonValue& root = result.value;
	whiteFadeSeconds_ = static_cast<float>(root["whiteFadeSeconds"].AsDouble(whiteFadeSeconds_));
	whiteHoldSeconds_ = static_cast<float>(root["whiteHoldSeconds"].AsDouble(whiteHoldSeconds_));
}

void HubScene::SaveTuning() const {
	JsonValue root = JsonValue::MakeObject();
	root["whiteFadeSeconds"] = JsonValue(static_cast<double>(whiteFadeSeconds_));
	root["whiteHoldSeconds"] = JsonValue(static_cast<double>(whiteHoldSeconds_));
	JsonWriter::WriteFile(kTuningPath, root, { true, 2 });
}

void HubScene::OnImGuiTuning() {
#ifdef _DEBUG
	if (ImGui::Button("Save Tuning")) SaveTuning();
	ImGui::SameLine();
	if (ImGui::Button("Reload Tuning")) LoadTuning();
	ImGui::SameLine();
	ImGui::TextDisabled("%s", kTuningPath);
	ImGui::SeparatorText("To Title (white fade)");
	ImGui::DragFloat("White Fade (s)", &whiteFadeSeconds_, 0.05f, 0.05f, 5.0f);
	ImGui::DragFloat("White Hold (s)", &whiteHoldSeconds_, 0.05f, 0.0f, 5.0f);
#endif // _DEBUG
}
