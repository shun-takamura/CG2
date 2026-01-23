#include "SceneManager.h"
#include "BaseScene.h"
#include "TitleScene.h"
#include "GameScene.h"

SceneManager* SceneManager::GetInstance() {
	static SceneManager instance;
	return &instance;
}

void SceneManager::Initialize(
	SpriteManager* spriteManager,
	Object3DManager* object3DManager,
	DirectXCore* dxCore,
	SRVManager* srvManager,
	InputManager* input
) {
	spriteManager_ = spriteManager;
	object3DManager_ = object3DManager;
	dxCore_ = dxCore;
	srvManager_ = srvManager;
	input_ = input;
}

void SceneManager::Finalize() {
	// 現在のシーンを解放
	if (currentScene_) {
		currentScene_->Finalize();
		delete currentScene_;
		currentScene_ = nullptr;
	}
}

void SceneManager::ChangeScene(Scene scene) {
	// 現在のシーンがあれば解放
	if (currentScene_) {
		currentScene_->Finalize();
		delete currentScene_;
		currentScene_ = nullptr;
	}

	// 新しいシーンを生成
	switch (scene) {
	case TITLE:
		currentScene_ = new TitleScene();
		break;
	case GAME:
		currentScene_ = new GameScene();
		break;
	default:
		break;
	}

	// シーンに各マネージャーを設定
	if (currentScene_) {
		currentScene_->SetSpriteManager(spriteManager_);
		currentScene_->SetObject3DManager(object3DManager_);
		currentScene_->SetDirectXCore(dxCore_);
		currentScene_->SetSRVManager(srvManager_);
		currentScene_->SetInputManager(input_);

		// シーンの初期化
		currentScene_->Initialize();
	}
}

void SceneManager::Update() {
	if (currentScene_) {
		currentScene_->Update();
	}
}

void SceneManager::Draw() {
	if (currentScene_) {
		currentScene_->Draw();
	}
}
