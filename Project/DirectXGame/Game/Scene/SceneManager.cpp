#include "SceneManager.h"
#include "BaseScene.h"
#include "AbstractSceneFactory.h"
#include "TransitionManager.h"
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

	// トランジションマネージャーの初期化
	TransitionManager::GetInstance()->Initialize(spriteManager, dxCore);
}

void SceneManager::Finalize() {
	TransitionManager::GetInstance()->Finalize();

	if (currentScene_) {
		currentScene_->Finalize();
	}
}

void SceneManager::Update() {
	// トランジションの更新
	TransitionManager::GetInstance()->Update();

	// 即時切り替えのシーンがあれば処理
	if (nextScene_) {
		if (currentScene_) {
			currentScene_->Finalize();
		}
		currentScene_ = std::move(nextScene_);
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

	// トランジションの描画（最前面）
	TransitionManager::GetInstance()->Draw();
}

void SceneManager::ChangeScene(const std::string& sceneName, TransitionType transitionType) {
	assert(sceneFactory_);

	if (TransitionManager::GetInstance()->IsTransitioning()) {
		return;
	}

	pendingSceneName_ = sceneName;

	TransitionManager::GetInstance()->StartTransition(
		transitionType,
		currentSceneName_,
		sceneName,
		[this]() { ExecuteSceneChange(); }
	);
}

void SceneManager::ChangeSceneRandom(const std::string& sceneName) {
	assert(sceneFactory_);

	if (TransitionManager::GetInstance()->IsTransitioning()) {
		return;
	}

	pendingSceneName_ = sceneName;

	TransitionManager::GetInstance()->StartRandomTransition(
		currentSceneName_,
		sceneName,
		[this]() { ExecuteSceneChange(); }
	);
}

void SceneManager::ChangeSceneImmediate(const std::string& sceneName) {
	assert(sceneFactory_);
	assert(nextScene_ == nullptr);

	pendingSceneName_ = sceneName;
	nextScene_ = sceneFactory_->CreateScene(sceneName);
	currentSceneName_ = sceneName;
}

void SceneManager::ExecuteSceneChange() {
	if (pendingSceneName_.empty()) return;

	if (currentScene_) {
		currentScene_->Finalize();
	}

	currentScene_ = sceneFactory_->CreateScene(pendingSceneName_);
	currentSceneName_ = pendingSceneName_;

	SetupScene(currentScene_.get());
	currentScene_->Initialize();

	pendingSceneName_.clear();
}

bool SceneManager::IsTransitioning() const {
	return TransitionManager::GetInstance()->IsTransitioning();
}

TransitionType SceneManager::GetLastUsedTransitionType() const {
	return TransitionManager::GetInstance()->GetLastTransitionRecord().type;
}

std::string SceneManager::GetLastUsedTransitionName() const {
	return TransitionManager::GetInstance()->GetLastTransitionRecord().transitionName;
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