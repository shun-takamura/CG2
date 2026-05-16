#include "SceneManager.h"
#include "BaseScene.h"
#include "AbstractSceneFactory.h"
#include "TransitionManager.h"
#include "DStorageManager.h"
#include <cassert>

#ifdef _DEBUG
#include "ImGuiManager.h"
#endif

SceneManager* SceneManager::GetInstance() {
	static SceneManager instance;
	return &instance;
}

void SceneManager::Initialize(
	SpriteManager* spriteManager,
	Object3DManager* object3DManager,
	SkyboxManager* skyboxManager,
	DirectXCore* dxCore,
	SRVManager* srvManager,
	InputManager* input,
	SkinningComputeManager* skinningComputeManager
) {
	spriteManager_ = spriteManager;
	object3DManager_ = object3DManager;
	skyboxManager_ = skyboxManager;
	dxCore_ = dxCore;
	srvManager_ = srvManager;
	input_ = input;
	skinningComputeManager_ = skinningComputeManager;

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
		// シーン読み込みは DStorage バッチで囲む
		DStorageManager::GetInstance()->BeginBatch();
		currentScene_->Initialize();
		DStorageManager::GetInstance()->EndBatchAndWait();
#ifdef _DEBUG
		ImGuiManager::Instance().SetCamera(currentScene_->GetCamera());
#endif
	}

	// 現在のシーンを更新
	if (currentScene_) {
		currentScene_->Update();

		// シーン共通サービス（カメラプレビュー / QR）の更新
		currentScene_->UpdateSceneServices();

		// 非同期ロードキューの GPU フェーズを進める（毎フレーム1件）
		currentScene_->ProcessAsyncLoads();
	}
}

void SceneManager::Draw() {
	if (currentScene_) {
		currentScene_->Draw();

		// シーン共通サービス（カメラプレビュー）の描画
		currentScene_->DrawSceneServices();
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
	// シーン読み込みは DStorage バッチで囲む
	DStorageManager::GetInstance()->BeginBatch();
	currentScene_->Initialize();
	DStorageManager::GetInstance()->EndBatchAndWait();
#ifdef _DEBUG
	ImGuiManager::Instance().SetCamera(currentScene_->GetCamera());
#endif

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
		scene->SetSkinningComputeManager(skinningComputeManager_);
		scene->SetSkyboxManager(skyboxManager_);
		scene->SetDirectXCore(dxCore_);
		scene->SetSRVManager(srvManager_);
		scene->SetInputManager(input_);
	}
}