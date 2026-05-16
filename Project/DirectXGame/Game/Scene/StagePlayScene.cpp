#include "StagePlayScene.h"

#include "Camera.h"
#include "Object3DManager.h"
#include "SceneManager.h"
#include "TransitionManager.h"
#include "InputManager.h"
#include "KeyboardInput.h"
#include "Game.h"

StagePlayScene::StagePlayScene() = default;
StagePlayScene::~StagePlayScene() = default;

void StagePlayScene::Initialize() {
	Game::GetPostEffect()->ResetEffects();

	camera_ = std::make_unique<Camera>();
	camera_->SetTranslate({ 0.0f, 0.0f, -20.0f });
	camera_->SetRotate({ 0.0f, 0.0f, 0.0f });
	object3DManager_->SetDefaultCamera(camera_.get());

	phase_ = Phase::Rail;
	paused_ = false;
}

void StagePlayScene::Finalize() {}

void StagePlayScene::Update() {
	if (SceneManager::GetInstance()->IsTransitioning()) {
		return;
	}

	auto* kb = input_->GetKeyboard();

	// ESC でポーズトグル（メニュー実装は後で）
	if (kb->TriggerKey(DIK_ESCAPE)) {
		paused_ = !paused_;
	}
	if (paused_) {
		return;
	}

	// 仮: 1キーでクリア相当(Result行き)、2キーでHubへ戻る
	if (kb->TriggerKey(DIK_1)) {
		SceneManager::GetInstance()->ChangeScene("RESULT", TransitionType::Fade);
		return;
	}
	if (kb->TriggerKey(DIK_2)) {
		SceneManager::GetInstance()->ChangeScene("HUB", TransitionType::Fade);
		return;
	}

	// 仮: Tab でフェーズ切替（着地遷移のテスト用）
	if (kb->TriggerKey(DIK_TAB)) {
		switch (phase_) {
		case Phase::Rail:    phase_ = Phase::Landing; break;
		case Phase::Landing: phase_ = Phase::Boss;    break;
		case Phase::Boss:    phase_ = Phase::Rail;    break;
		}
	}

	camera_->Update();
}

void StagePlayScene::Draw() {
	// 各フェーズの描画は後で追加
}

Camera* StagePlayScene::GetCamera() {
	return camera_.get();
}
