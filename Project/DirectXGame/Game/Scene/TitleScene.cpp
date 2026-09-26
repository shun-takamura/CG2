#include "TitleScene.h"

#include "Camera.h"
#include "Object3DManager.h"
#include "Object3DInstance.h"
#include "Skybox.h"
#include "LightManager.h"
#include "SceneManager.h"
#include "TransitionManager.h"
#include "InputManager.h"
#include "InputAction.h"
#include "Config/GameActions.h"
#include "Game.h"
#include "DirectXCore.h"
#include "PostEffect.h"
#include "VignetteEffect.h"
#include "RadialBlurEffect.h"
#include "TextRenderer.h"
#include <Windows.h>
#include <algorithm>
#include <cmath>

#ifdef _DEBUG
#include "imgui.h"
#endif

namespace {
	// 背景は STG 冒頭セクションと同じ空を流用する（タイトル専用アセットを作らない）
	constexpr const char* kTitleSkyboxPath = "Resources/Cubemaps/rogland_clear_night_8k.dds";

	enum TitleMenuItem : int {
		kTitleStart = 0,
		kTitleQuit,
	};

	// 終端で少しだけ行き過ぎて戻る ease-out-back（ロゴの「ポン」と出る感じ）
	float EaseOutBack(float t) {
		const float c1 = 1.70158f;
		const float c3 = c1 + 1.0f;
		const float u = t - 1.0f;
		return 1.0f + c3 * u * u * u + c1 * u * u;
	}
}

TitleScene::TitleScene() = default;
TitleScene::~TitleScene() = default;

void TitleScene::Initialize() {
	Game::GetPostEffect()->ResetEffects();

	camera_ = std::make_unique<Camera>();
	camera_->SetTranslate({ 0.0f, 0.0f, 0.0f });
	camera_->SetRotate({ cameraPitch_, 0.0f, 0.0f });
	object3DManager_->SetDefaultCamera(camera_.get());
	skyboxManager_->SetDefaultCamera(camera_.get());

	skybox_ = std::make_unique<Skybox>();
	skybox_->Initialize(skyboxManager_, dxCore_, kTitleSkyboxPath);

	logo_ = std::make_unique<Object3DInstance>();
	logo_->Initialize(object3DManager_, dxCore_, "Resources/Models/Title", "title.mesh", "TitleLogo");

	// 平行光源の既定 intensity は 0 なので、ロゴを照らす光をここで必ず設定する
	if (auto* lm = LightManager::GetInstance(); lm && lm->GetDirectionalLightData()) {
		lm->SetDirectionalLightDirection({ 0.25f, -0.45f, 0.86f });
		lm->SetDirectionalLightColor({ 1.0f, 1.0f, 1.0f, 1.0f });
		lm->SetDirectionalLightIntensity(1.2f);
	}

	if (auto* pe = Game::GetPostEffect()) {
		if (pe->vignette) {
			pe->vignette->SetEnabled(true);
			pe->vignette->SetIntensity(vignetteIntensity_);
		}
		if (pe->radialBlur) {
			pe->radialBlur->SetEnabled(true);
			pe->radialBlur->SetCenter(0.5f, 0.5f);
			pe->radialBlur->SetBlurWidth(introBlurWidth_);
		}
	}

	menu_.SetItems({ "スタート", "ゲーム終了" });
	menuOpen_ = false;
	elapsed_ = 0.0f;
	idleSeconds_ = 0.0f;
}

void TitleScene::Finalize() {
	Game::GetPostEffect()->ResetEffects();
}

void TitleScene::Update() {
	const float dt = GetScaledDeltaTime();
	elapsed_ += dt;

	// 遷移中も背景とロゴは動かし続ける（止まった画面を見せない）
	UpdateCameraAndLogo(dt);
	UpdateIntroPostEffect();

#ifdef _DEBUG
	OnImGuiTuning();
#endif

	if (SceneManager::GetInstance()->IsTransitioning()) {
		return;
	}

	UpdateInput();
}

void TitleScene::UpdateInput() {
	auto* actionMap = input_->GetActionMap();
	if (!actionMap) return;

	// ロゴ登場中の入力は受け付けない（演出を飛ばして即メニューにならないように）
	if (elapsed_ < introDuration_) return;

	if (!menuOpen_) {
		if (actionMap->AnyInputTriggered()) {
			// 押したフレームはメニュー入力を読まない（同じキーで即決定されるのを防ぐ）
			menuOpen_ = true;
			menu_.SetSelectedIndex(kTitleStart);
			idleSeconds_ = 0.0f;
			return;
		}
		// 無操作カウンタ更新（後でデモ動画再生のトリガーに使う）
		idleSeconds_ += GetScaledDeltaTime();
		(void)kDemoTriggerSeconds; // 警告抑制。動画再生実装時に使用
		return;
	}

	const VerticalMenu::Result result = menu_.Update(actionMap);
	if (result == VerticalMenu::Result::Canceled) {
		menuOpen_ = false;
		return;
	}
	if (result == VerticalMenu::Result::Confirmed) {
		switch (menu_.GetSelectedIndex()) {
		case kTitleStart:
			SceneManager::GetInstance()->ChangeScene("HUB", TransitionType::Stripe);
			return;
		case kTitleQuit:
			PostQuitMessage(0);
			return;
		}
	}
}

void TitleScene::UpdateCameraAndLogo(float dt) {
	// 背景：カメラをその場でゆっくり旋回させて空を流す
	Vector3 camRot = camera_->GetRotate();
	camRot.x = cameraPitch_;
	camRot.y += cameraYawSpeed_ * dt;
	camera_->SetRotate(camRot);
	camera_->Update();
	skybox_->Update(dt);

	// ロゴ：カメラ前方に置き、カメラの向きに合わせて正面を保つ
	const Vector3 camPos = camera_->GetTranslate();
	const Vector3 fwd = camera_->GetForward();
	const Vector3 up = camera_->GetUp();
	const float bob = std::sin(elapsed_ * logoBobSpeed_) * logoBobAmplitude_;
	const float lift = logoHeight_ + bob;
	logo_->SetTranslate({
		camPos.x + fwd.x * logoDistance_ + up.x * lift,
		camPos.y + fwd.y * logoDistance_ + up.y * lift,
		camPos.z + fwd.z * logoDistance_ + up.z * lift });

	const float sway = std::sin(elapsed_ * logoSwaySpeed_) * logoSwayAmplitude_;
	logo_->SetRotate({ logoBaseRotate_.x + camRot.x, logoBaseRotate_.y + camRot.y + sway, logoBaseRotate_.z });

	const float t = (introDuration_ > 0.0f) ? std::clamp(elapsed_ / introDuration_, 0.0f, 1.0f) : 1.0f;
	const float s = logoScale_ * EaseOutBack(t);
	logo_->SetScale({ s, s, s });
	logo_->Update();
}

void TitleScene::UpdateIntroPostEffect() {
	auto* pe = Game::GetPostEffect();
	if (!pe) return;
	if (pe->vignette) pe->vignette->SetIntensity(vignetteIntensity_);

	// 起動直後だけ強いラジアルブラーをかけ、ロゴ登場に合わせて収束させる
	if (pe->radialBlur && pe->radialBlur->IsEnabled()) {
		const float t = (introDuration_ > 0.0f) ? std::clamp(elapsed_ / introDuration_, 0.0f, 1.0f) : 1.0f;
		const float remain = 1.0f - t;
		pe->radialBlur->SetBlurWidth(introBlurWidth_ * remain * remain);
		if (t >= 1.0f) pe->radialBlur->SetEnabled(false);
	}
}

void TitleScene::Draw() {
	// Skybox を最初に描画（深度書き込みなしの ReadOnly DSV）
	auto* commandList = dxCore_->GetCommandList();
	auto rtvHandle = Game::GetPostEffect()->GetSceneRenderTarget()->GetRTVHandle();
	auto readOnlyDsv = dxCore_->GetReadOnlyDsvHandle();
	commandList->OMSetRenderTargets(1, &rtvHandle, false, &readOnlyDsv);

	skyboxManager_->DrawSetting();
	if (skybox_) skybox_->Draw(dxCore_);

	auto normalDsv = dxCore_->GetDsvHandle();
	commandList->OMSetRenderTargets(1, &rtvHandle, false, &normalDsv);

	object3DManager_->DrawSetting();
	LightManager::GetInstance()->BindLights(commandList);
	if (logo_) logo_->Draw(dxCore_);

	TextRenderer* tr = TextRenderer::GetInstance();
	if (!tr->IsInitialized()) return;

	const float screenW = static_cast<float>(dxCore_->GetSwapChainWidth());
	const float screenH = static_cast<float>(dxCore_->GetSwapChainHeight());

	if (elapsed_ >= introDuration_) {
		if (menuOpen_) {
			menu_.Draw({ screenW * 0.5f, screenH * 0.72f });
		} else {
			// ゆっくり明滅させて「押せる」ことを伝える
			const float blink = 0.35f + 0.65f * (0.5f + 0.5f * std::sin((elapsed_ - introDuration_) * 3.0f));
			const char* label = "PRESS ANY BUTTON";
			const float scale = 1.2f;
			const float w = tr->MeasureWidth(label, scale);
			tr->DrawText(label, { (screenW - w) * 0.5f, screenH * 0.72f }, scale,
				{ 1.0f, 1.0f, 1.0f, blink }, 2.0f, { 0.0f, 0.0f, 0.0f, blink });
		}
	}
	tr->Flush();
}

void TitleScene::OnImGuiTuning() {
#ifdef _DEBUG
	if (ImGui::Begin("Title Tuning")) {
		ImGui::DragFloat("Camera Yaw Speed", &cameraYawSpeed_, 0.005f, -1.0f, 1.0f);
		ImGui::DragFloat("Camera Pitch", &cameraPitch_, 0.01f, -1.5f, 1.5f);
		ImGui::Separator();
		ImGui::DragFloat("Logo Distance", &logoDistance_, 0.1f, 1.0f, 50.0f);
		ImGui::DragFloat("Logo Height", &logoHeight_, 0.05f, -10.0f, 10.0f);
		ImGui::DragFloat("Logo Scale", &logoScale_, 0.05f, 0.1f, 20.0f);
		ImGui::DragFloat3("Logo Base Rotate", &logoBaseRotate_.x, 0.01f);
		ImGui::DragFloat("Bob Amplitude", &logoBobAmplitude_, 0.01f, 0.0f, 2.0f);
		ImGui::DragFloat("Bob Speed", &logoBobSpeed_, 0.05f, 0.0f, 10.0f);
		ImGui::DragFloat("Sway Amplitude", &logoSwayAmplitude_, 0.01f, 0.0f, 1.0f);
		ImGui::DragFloat("Sway Speed", &logoSwaySpeed_, 0.05f, 0.0f, 10.0f);
		ImGui::Separator();
		ImGui::DragFloat("Intro Duration", &introDuration_, 0.05f, 0.0f, 5.0f);
		ImGui::DragFloat("Intro Blur Width", &introBlurWidth_, 0.005f, 0.0f, 0.5f);
		ImGui::DragFloat("Vignette", &vignetteIntensity_, 0.01f, 0.0f, 2.0f);
		if (ImGui::Button("Replay Intro")) {
			elapsed_ = 0.0f;
			menuOpen_ = false;
			if (auto* pe = Game::GetPostEffect(); pe && pe->radialBlur) pe->radialBlur->SetEnabled(true);
		}
	}
	ImGui::End();
#endif
}

Camera* TitleScene::GetCamera() {
	return camera_.get();
}
