#include "SceneManager.h"
#include "BaseScene.h"
#include "AbstractSceneFactory.h"
#include <cassert>

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
	}
}

void SceneManager::Update() {
	// 次のシーンが予約されていたら切り替え
	if (nextScene_) {
		// 現在のシーンがあれば解放
		if (currentScene_) {
			currentScene_->Finalize();
			
		}

		// 次のシーンを現在のシーンに
		currentScene_ = std::move(nextScene_);

		// シーンのセットアップと初期化
		SetupScene(currentScene_.get());
		currentScene_->Initialize();
	}

	// 現在のシーンを更新
	if (currentScene_) {
		currentScene_->Update();
	}
}

void SceneManager::Draw() {
	if (currentScene_) {
		currentScene_->Draw();
	}
}

void SceneManager::ChangeScene(const std::string& sceneName) {
	assert(sceneFactory_);
	assert(nextScene_ == nullptr);

	// 次シーンを生成
	nextScene_ = sceneFactory_->CreateScene(sceneName);
}

void SceneManager::SetupScene(BaseScene* scene) {
	if (scene) {
		scene->SetSpriteManager(spriteManager_);
		scene->SetObject3DManager(object3DManager_);
		scene->SetDirectXCore(dxCore_);
		scene->SetSRVManager(srvManager_);
		scene->SetInputManager(input_);
	}
}