#include "SceneManager.h"
#include "Scene.h"
#include "EngineTime.h"
#include "AbstractSceneFactory.h"
#include "TransitionManager.h"
#include "DStorageManager.h"
#include "Components/CollisionManager.h"
#include "SessionLogger.h"
#include "PepperMacros.h"
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

	// エンジン側の時間スケール供給元を解除（dangling 参照防止）
	EngineTime::SetProvider(nullptr);
}

void SceneManager::Update() {
	PEPPER_SCOPE("SceneManager::Update");

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
		// P.E.P.P.E.R. へオブジェクト/リソース数を報告（USE_PEPPER 未定義時は空）
		currentScene_->ReportProfileGauges();

		currentScene_->Update();

		// シーン経過秒の加算（Debugシークバー / 仕掛けタイミングの基準）
		currentScene_->TickElapsedSeconds(currentScene_->GetScaledDeltaTime());

		// 全コライダーの当たり判定（Update 後、位置が確定したタイミングで）
		CollisionManager::GetInstance()->Update();

		// シーン共通サービス（カメラプレビュー / QR）の更新
		currentScene_->UpdateSceneServices();

		// 非同期ロードキューの GPU フェーズを進める（毎フレーム1件）
		currentScene_->ProcessAsyncLoads();
	}
}

void SceneManager::Draw() {
	PEPPER_SCOPE("SceneManager::Draw");

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

	SessionLogger::Instance().Write(SessionLogger::Category::Event, SessionLogger::Level::Info,
		"SCENE_CHANGE from=" + (currentSceneName_.empty() ? std::string("(none)") : currentSceneName_) + " to=" + sceneName + " immediate=1");

	pendingSceneName_ = sceneName;
	nextScene_ = sceneFactory_->CreateScene(sceneName);
	currentSceneName_ = sceneName;
}

void SceneManager::ExecuteSceneChange() {
	if (pendingSceneName_.empty()) return;

	if (currentScene_) {
		currentScene_->Finalize();
	}

	SessionLogger::Instance().Write(SessionLogger::Category::Event, SessionLogger::Level::Info,
		"SCENE_CHANGE from=" + (currentSceneName_.empty() ? std::string("(none)") : currentSceneName_) + " to=" + pendingSceneName_);

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

void SceneManager::SetupScene(Scene* scene) {
	if (scene) {
		scene->SetSpriteManager(spriteManager_);
		scene->SetObject3DManager(object3DManager_);
		scene->SetSkinningComputeManager(skinningComputeManager_);
		scene->SetSkyboxManager(skyboxManager_);
		scene->SetDirectXCore(dxCore_);
		scene->SetSRVManager(srvManager_);
		scene->SetInputManager(input_);

		// このシーンをエンジン側の時間スケール供給元として登録（Scene は ITimeScaleProvider）
		EngineTime::SetProvider(scene);
	}
}