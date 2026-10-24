#include "SceneManager.h"
#include "SceneRoutes.h"
#include "Scene.h"
#include "EngineTime.h"
#include "AbstractSceneFactory.h"
#include "TransitionManager.h"
#include "AttractMode.h"
#include "InputManager.h"
#include "InputAction.h"
#include "DStorageManager.h"
#include "Components/CollisionManager.h"
#include "SessionLogger.h"
#include "PepperMacros.h"
#include "DirectXCore.h"
#include "Profiling/LoadProfiler.h"
#include "ScenePreloader.h"
#include "Log.h"
#include <cassert>
#include <format>

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
	ScenePreloader::GetInstance()->Initialize(dxCore);
}

void SceneManager::Finalize() {
	// 裏の読み込みスレッドを先に止める（各マネージャーより前に）
	ScenePreloader::GetInstance()->Finalize();
	TransitionManager::GetInstance()->Finalize();

	if (currentScene_) {
		currentScene_->Finalize();
	}

	// エンジン側の時間スケール供給元を解除（dangling 参照防止）
	EngineTime::SetProvider(nullptr);
}

void SceneManager::Update() {
	PEPPER_SCOPE("SceneManager::Update");

	// トランジションの更新。飛ばせる種類（羽根の着水）は何か押したら残りを飛ばす（19 番 §4.2.3。デモモード中は飛ばさない）
	TransitionManager* transitions = TransitionManager::GetInstance();
	if (transitions->IsSkippable() && input_ && !AttractMode::GetInstance()->IsEnabled()) {
		const InputActionMap* actions = input_->GetActionMap();
		if (actions && actions->AnyInputTriggered()) transitions->RequestSkip();
	}
	transitions->Update();

	// 次のシーンの裏での先読み（GPU フェーズを少しずつ）
	ScenePreloader::GetInstance()->Update();

	// 即時切り替えのシーンがあれば処理
	if (nextScene_) {
		MarkSceneChange(SceneChangeTiming::ChangeBegin);
		if (currentScene_) {
			currentScene_->Finalize();
		}
		MarkSceneChange(SceneChangeTiming::Finalized);
		// 即時切り替えでは生成は ChangeSceneImmediate 側で済んでいるので Create 区間は 0 扱い
		MarkSceneChange(SceneChangeTiming::Created);
		currentScene_ = std::move(nextScene_);
		SetupScene(currentScene_.get());
		LoadCurrentScene();
	}

	// 現在のシーンを更新
	if (currentScene_) {
		// P.E.P.P.E.R. へオブジェクト/リソース数を報告（USE_PEPPER 未定義時は空）
		currentScene_->ReportProfileGauges();

		currentScene_->Update();

		if (changeTiming_.waitingFirstUpdate) {
			MarkSceneChange(SceneChangeTiming::FirstUpdated);
			changeTiming_.waitingFirstUpdate = false;
		}

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
	ChangeScene(sceneName, transitionType, transitionType);
}

void SceneManager::ChangeScene(const std::string& sceneName) {
	const SceneRoutes::Transitions t = SceneRoutes::Find(currentSceneName_, sceneName);
	ChangeScene(sceneName, t.cover, t.reveal);
}

void SceneManager::ChangeScene(const std::string& sceneName, TransitionType coverType, TransitionType revealType) {
	assert(sceneFactory_);

	if (TransitionManager::GetInstance()->IsTransitioning()) {
		return;
	}

	// 覆う側も開く側もシーンが演じる（③ リフトの縦穴。16_StageStart.md §4.2）：暗転させずにそのまま差し替える。
	// ロードの間は前のシーンの最後の1枚が残る（同期ロード）。行き先のシーンは前のシーンの続きの見え方で始める取り決め
	if (coverType == TransitionManager::kCoverByScene && revealType == TransitionManager::kRevealByScene) {
		if (!nextScene_) ChangeSceneImmediate(sceneName);
		return;
	}

	pendingSceneName_ = sceneName;
	MarkSceneChange(SceneChangeTiming::Request);

	TransitionManager::GetInstance()->StartTransition(
		coverType,
		revealType,
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
	MarkSceneChange(SceneChangeTiming::Request);

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
	MarkSceneChange(SceneChangeTiming::Request);
	nextScene_ = sceneFactory_->CreateScene(sceneName);
	currentSceneName_ = sceneName;
}

void SceneManager::ExecuteSceneChange() {
	if (pendingSceneName_.empty()) return;

	MarkSceneChange(SceneChangeTiming::ChangeBegin);
	if (currentScene_) {
		currentScene_->Finalize();
	}
	MarkSceneChange(SceneChangeTiming::Finalized);

	SessionLogger::Instance().Write(SessionLogger::Category::Event, SessionLogger::Level::Info,
		"SCENE_CHANGE from=" + (currentSceneName_.empty() ? std::string("(none)") : currentSceneName_) + " to=" + pendingSceneName_);

	currentScene_ = sceneFactory_->CreateScene(pendingSceneName_);
	currentSceneName_ = pendingSceneName_;
	MarkSceneChange(SceneChangeTiming::Created);

	SetupScene(currentScene_.get());
	LoadCurrentScene();

	pendingSceneName_.clear();
}

void SceneManager::PreloadScene(const std::string& sceneName) {
	ScenePreloader::GetInstance()->Begin(sceneName);
}

bool SceneManager::IsPreloading() const {
	return ScenePreloader::GetInstance()->IsBusy();
}

void SceneManager::LoadCurrentScene() {
	LoadProfiler* lp = LoadProfiler::GetInstance();
	const size_t phaseBegin = lp->GetPhases().size();

	// 先読みの途中なら残りを済ませる（裏のスレッドが CPU フェーズの途中の物を同期ロードで触らないように）
	{
		LOAD_PHASE("SceneLoad/PreloadRemainder");
		ScenePreloader::GetInstance()->CompleteNow();
	}

	const DirectXCore::ShaderLoadStats shaderBegin = dxCore_ ? dxCore_->GetShaderLoadStats() : DirectXCore::ShaderLoadStats{};

	// シーン読み込みは DStorage バッチで囲む（ロード時間を SceneLoad:<名前> として計測）
	{
		LOAD_PHASE("SceneLoad:" + currentSceneName_);
		DStorageManager::GetInstance()->BeginBatch();
#ifdef _DEBUG
		// 読まれた物を記録して先読みのマニフェストにする（Resources/Json/Preload/<シーン名>.json）
		ScenePreloader::GetInstance()->BeginRecording();
#endif
		currentScene_->Initialize();
#ifdef _DEBUG
		ScenePreloader::GetInstance()->EndRecording(currentSceneName_);
#endif
		{
			LOAD_PHASE("SceneLoad/DStorageWait");
			DStorageManager::GetInstance()->EndBatchAndWait();
		}
		// 計測中だけ GPU 転送の完了まで区間に含める（FS も DStorage と同条件で比べるため）
		if (dxCore_ && lp->IsMeasurementMode()) dxCore_->FlushCommandList();
	}
	MarkSceneChange(SceneChangeTiming::Initialized);

	// 読み込みの内訳（シーンの Initialize が LOAD_PHASE で区切った区間）を profile.log へ。区間は閉じた順に並ぶ
	const auto& phases = lp->GetPhases();
	for (size_t i = phaseBegin; i < phases.size(); ++i) {
		SessionLogger::Instance().Write(SessionLogger::Category::Profile, SessionLogger::Level::Info,
			std::format("LOAD kind=scene_phase scene={} name={} wall_ms={:.2f} cpu_ms={:.2f}",
				currentSceneName_, phases[i].name, phases[i].wallMs, phases[i].cpuMs));
	}
	if (dxCore_) {
		const DirectXCore::ShaderLoadStats s = dxCore_->GetShaderLoadStats();
		SessionLogger::Instance().Write(SessionLogger::Category::Profile, SessionLogger::Level::Info,
			std::format("LOAD kind=scene_shaders scene={} loaded={} cache_hits={} load_ms={:.2f}",
				currentSceneName_, s.loadCount - shaderBegin.loadCount, s.cacheHits - shaderBegin.cacheHits,
				s.loadMs - shaderBegin.loadMs));
	}
#ifdef _DEBUG
	ImGuiManager::Instance().SetCamera(currentScene_->GetCamera());
#endif
}

void SceneManager::MarkSceneChange(SceneChangeTiming::Mark mark) {
	SceneChangeTiming& t = changeTiming_;
	if (mark == SceneChangeTiming::Request) {
		t = {};
		t.active = true;
		t.from = currentSceneName_.empty() ? std::string("(none)") : currentSceneName_;
		t.to = pendingSceneName_;
	}
	if (!t.active) return;

	t.wallMs[mark] = LoadProfiler::NowMs();
	t.cpuMs[mark] = LoadProfiler::ProcessCpuMs();
	if (mark == SceneChangeTiming::Initialized) t.waitingFirstUpdate = true;
}

void SceneManager::OnFramePresented() {
	SceneChangeTiming& t = changeTiming_;
	// 最初の Update を通った後のフレームだけ（遷移演出中の Present は対象外）
	if (!t.active || t.waitingFirstUpdate || t.wallMs[SceneChangeTiming::FirstUpdated] == 0.0) return;

	MarkSceneChange(SceneChangeTiming::Presented);
	LogSceneChangeTiming();
	t.active = false;
}

void SceneManager::LogSceneChangeTiming() const {
	using T = SceneChangeTiming;
	const T& t = changeTiming_;
	auto wall = [&](T::Mark a, T::Mark b) { return t.wallMs[b] - t.wallMs[a]; };
	auto cpu = [&](T::Mark a, T::Mark b) { return t.cpuMs[b] - t.cpuMs[a]; };

	const double totalMs = wall(T::Request, T::Presented);
	const double loadMs = wall(T::ChangeBegin, T::Presented);

	Log(std::format(
		"[LOAD] scene_change {}->{} total={:.1f}ms (transition={:.1f} / finalize={:.1f} / create={:.1f} / init={:.1f} / first_update={:.1f} / first_draw_present={:.1f}) load_only={:.1f}ms cpu={:.1f}ms\n",
		t.from, t.to, totalMs,
		wall(T::Request, T::ChangeBegin), wall(T::ChangeBegin, T::Finalized), wall(T::Finalized, T::Created),
		wall(T::Created, T::Initialized), wall(T::Initialized, T::FirstUpdated), wall(T::FirstUpdated, T::Presented),
		loadMs, cpu(T::ChangeBegin, T::Presented)));

	// profile.log（P.E.P.P.E.R.）にも LOAD 行で残す
	SessionLogger::Instance().Write(SessionLogger::Category::Profile, SessionLogger::Level::Info,
		std::format(
			"LOAD kind=scene_change from={} to={} total_ms={:.2f} transition_ms={:.2f} finalize_ms={:.2f} create_ms={:.2f} init_ms={:.2f} first_update_ms={:.2f} first_draw_present_ms={:.2f} load_only_ms={:.2f} load_only_cpu_ms={:.2f} init_cpu_ms={:.2f}",
			t.from, t.to, totalMs,
			wall(T::Request, T::ChangeBegin), wall(T::ChangeBegin, T::Finalized), wall(T::Finalized, T::Created),
			wall(T::Created, T::Initialized), wall(T::Initialized, T::FirstUpdated), wall(T::FirstUpdated, T::Presented),
			loadMs, cpu(T::ChangeBegin, T::Presented), cpu(T::Created, T::Initialized)));
}

bool SceneManager::IsTransitioning() const {
	return TransitionManager::GetInstance()->IsTransitioning();
}

TransitionState SceneManager::GetTransitionPhase() const {
	return TransitionManager::GetInstance()->GetPhase();
}

bool SceneManager::IsTransitionCovering() const {
	return TransitionManager::GetInstance()->IsCovering();
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