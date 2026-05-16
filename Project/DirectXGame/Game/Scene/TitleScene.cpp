#include "TitleScene.h"

#include "Camera.h"
#include "Object3DManager.h"
#include "SceneManager.h"
#include "TransitionManager.h"
#include "InputManager.h"
#include "InputAction.h"
#include "Game.h"

TitleScene::TitleScene() = default;
TitleScene::~TitleScene() = default;

void TitleScene::Initialize() {
	Game::GetPostEffect()->ResetEffects();

	camera_ = std::make_unique<Camera>();
	camera_->SetTranslate({ 0.0f, 0.0f, -10.0f });
	camera_->SetRotate({ 0.0f, 0.0f, 0.0f });
	object3DManager_->SetDefaultCamera(camera_.get());

	idleSeconds_ = 0.0f;
}

void TitleScene::Finalize() {}

void TitleScene::Update() {
	if (SceneManager::GetInstance()->IsTransitioning()) {
		return;
	}

	// Press Any Button: 何らかの物理入力があれば Hub へ
	auto* actionMap = input_->GetActionMap();
	if (actionMap && actionMap->AnyInputTriggered()) {
		SceneManager::GetInstance()->ChangeScene("HUB", TransitionType::Fade);
		return;
	}

	// 無操作カウンタ更新（後でデモ動画再生のトリガーに使う）
	idleSeconds_ += GetScaledDeltaTime();
	(void)kDemoTriggerSeconds; // 警告抑制。動画再生実装時に使用

	camera_->Update();
}

void TitleScene::Draw() {
	// 表示物は後で追加
}

Camera* TitleScene::GetCamera() {
	return camera_.get();
}
