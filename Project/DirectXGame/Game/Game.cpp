#include "Game.h"
#include "SoundManager.h"
#include <filesystem>
#include <algorithm>

#include "DirectXTex.h"
#include "d3dx12.h"

//============================
// 自作ヘッダーのインクルード
//============================
#include "WindowsApplication.h"
#include <shellapi.h> // CommandLineToArgvW（--start-scene）
#include <cwchar>
#include "DirectXCore.h"
#include "SpriteManager.h"
#include "Object3DManager.h"
#include "SRVManager.h"
#include "InputManager.h"
#include "ImGuiManager.h"
#include "IImGuiWindow.h"
#include "Editor/GameInspectorSections.h"
#include "Editor/GameAssetBrowserSections.h"
#include "StagePlayScene.h"
#include "HubScene.h"
#include "KeyboardInput.h"
#include "SceneManager.h"
#include "SceneFactory.h"
#include "IImGuiEditable.h"
#include "Components/Gameplay.h"
#include "Components/CollisionManager.h"
#include "CameraCapture.h"
#include "QRCodeReader.h"
#include "TransitionManager.h"
#include "SceneRoutes.h"
#include "TextRenderer.h"
#include "AttractMode.h"
#include "Camera.h"
#include "DStorageManager.h"
#include "InputAction.h"
#include "Config/KeyConfig.h"
#include "GPUParticleManager.h"
#include "Effect/EffectManager.h"
#include "Effect/EffectEditorWindow.h"
#include "Components/PrefabManager.h"
#include "Score/GameSession.h"
#include "Scene/Scene.h"
#include "LightManager.h"
#include "PepperMacros.h"
#include <memory>

std::unique_ptr<PostEffect> Game::postEffect_ = nullptr;
std::unique_ptr<GPUParticleManager> Game::gpuParticleManager_ = nullptr;

namespace {
	// --render-res の幅・高さを読むときの基数（10 進数）
	constexpr int kDecimalRadix = 10;
	// ブルームのフェードで 1 フレームに進める時間の上限を決めるフレームレート [fps]（重いフレームで一気に消えないように）
	constexpr float kBloomFadeMinFps = 30.0f;

	/// <summary>
	/// ゲームで使う SE・BGM を SoundManager に読み込む。SoundManager::LoadFile はファイルが無いと assert で落ちるので、
	/// 存在するものだけ読む（BGM を配置する前でも起動できる。鳴らす側は未ロード名なら何もしない）
	/// </summary>
	void LoadGameSounds() {
		auto* sm = SoundManager::GetInstance();
		static const char* const kSE[] = {
			"se_shot", "se_charge_shot", "se_hit", "se_explode", "se_player_damage",
			"se_dodge", "se_just_dodge", "se_melee", "se_boss_telegraph", "se_shockwave",
			"se_special", "se_ui_cursor", "se_ui_decide", "se_ui_cancel",
		};
		for (const char* name : kSE) {
			const std::string path = std::string("Resources/Sounds/SE/") + name + ".wav";
			if (std::filesystem::exists(path)) sm->LoadFile(name, path);
		}
		// 魔王魂は二次配布時にファイル名へ maoudamashii を含める決まり（クレジットは Documents/Readme.md）
		struct Bgm { const char* name; const char* file; };
		static const Bgm kBGM[] = {
			{ "bgm_title", "maoudamashii-title.mp3" },
			{ "bgm_stage", "maoudamashii-stage.mp3" },
			{ "bgm_boss",  "maoudamashii-boss.mp3" },
		};
		for (const Bgm& b : kBGM) {
			const std::string path = std::string("Resources/Sounds/BGM/") + b.file;
			if (std::filesystem::exists(path)) sm->LoadFile(b.name, path);
		}
		// タイトル画面専用（クレジットは Documents/Readme.md）。HUB は bgm_title のまま
		struct TitleSound { const char* name; const char* path; };
		static const TitleSound kTitleSounds[] = {
			{ "bgm_title_scene",    "Resources/Sounds/TitleScene/BGM/TitleBGM.mp3" },
			{ "se_title_open_door", "Resources/Sounds/TitleScene/SE/OpenDoor.mp3" },
			{ "se_title_in_door",   "Resources/Sounds/TitleScene/SE/InDoor.mp3" },
		};
		for (const TitleSound& t : kTitleSounds) {
			if (std::filesystem::exists(t.path)) sm->LoadFile(t.name, t.path);
		}
	}

#ifdef _DEBUG
	/// <summary>
	/// TimeControler の「Scene Switch」欄（EditorHostHooks::drawSceneSwitcher）を描く。
	/// 行き先を選んで、ゲームと同じ組み合わせ（SceneRoutes）か、手で選んだトランジションで切り替える。
	/// STAGEPLAY からはクリア / 失敗を選ぶと、ゲームが実際に使う行き先（RESULT / GAMEOVER）になる
	/// </summary>
	void DrawSceneSwitcher() {
		static const char* const kScenes[] = { "TITLE", "HUB", "STAGESELECT", "STAGEPLAY", "RESULT", "GAMEOVER", "DEMO" };
		struct TransitionChoice { const char* name; TransitionType type; };
		// 覆う側は「シーンが覆い終えた（覆い切った所から始める）」も選べる
		static const TransitionChoice kCovers[] = {
			{ "Stripe", TransitionType::Stripe }, { "Fade", TransitionType::Fade }, { "Ink", TransitionType::Ink }, { "FeatherLand", TransitionType::FeatherLand }, { "FeatherRise", TransitionType::FeatherRise },
			{ "None (scene covered)", TransitionManager::kCoverByScene },
		};
		// 開く側は「行き先のシーンに任せる（開く演出なし）」も選べる
		static const TransitionChoice kReveals[] = {
			{ "Stripe", TransitionType::Stripe }, { "Fade", TransitionType::Fade }, { "Ink", TransitionType::Ink }, { "FeatherLand", TransitionType::FeatherLand }, { "FeatherRise", TransitionType::FeatherRise },
			{ "None (scene reveals)", TransitionManager::kRevealByScene },
		};
		const auto nameOf = [](TransitionType type) -> const char* {
			if (type == TransitionType::None) return "None (scene)";
			for (const TransitionChoice& c : kReveals) {
				if (c.type == type) return c.name;
			}
			return "?";
		};

		static int sceneIndex = 1;
		static int stageOutcome = -1;   // -1 = 選ばない / 0 = クリア / 1 = 失敗
		static bool overrideTransition = false;
		static int coverIndex = 0;
		static int revealIndex = 0;

		auto* sm = SceneManager::GetInstance();
		const std::string& current = sm->GetCurrentSceneName();

		// 行き先：STAGEPLAY 中はクリア / 失敗のどちらかに印を付けると、そちらが優先
		const char* destination = kScenes[sceneIndex];
		if (current == "STAGEPLAY") {
			bool clear = (stageOutcome == 0);
			bool fail = (stageOutcome == 1);
			if (ImGui::Checkbox("Clear", &clear)) stageOutcome = clear ? 0 : -1;
			ImGui::SameLine();
			if (ImGui::Checkbox("Fail", &fail)) stageOutcome = fail ? 1 : -1;
			if (stageOutcome == 0) destination = "RESULT";
			if (stageOutcome == 1) destination = "GAMEOVER";
		}
		ImGui::BeginDisabled(current == "STAGEPLAY" && stageOutcome >= 0);
		ImGui::Combo("Destination", &sceneIndex, kScenes, IM_ARRAYSIZE(kScenes));
		ImGui::EndDisabled();

		// トランジション：既定は表（SceneRoutes）どおり。差し替えるときだけ覆う側・開く側を選ぶ
		const SceneRoutes::Transitions route = SceneRoutes::Find(current, destination);
		ImGui::Text("Route: %s -> %s  (cover %s / reveal %s)", current.c_str(), destination,
			nameOf(route.cover), nameOf(route.reveal));
		ImGui::Checkbox("Override Transition", &overrideTransition);
		if (overrideTransition) {
			const auto getName = [](void* data, int i) -> const char* {
				return static_cast<const TransitionChoice*>(data)[i].name;
			};
			ImGui::Combo("Cover", &coverIndex, getName, const_cast<TransitionChoice*>(kCovers), IM_ARRAYSIZE(kCovers));
			ImGui::Combo("Reveal", &revealIndex, getName, const_cast<TransitionChoice*>(kReveals), IM_ARRAYSIZE(kReveals));
		}

		ImGui::BeginDisabled(sm->IsTransitioning());
		if (ImGui::Button("Switch")) {
			if (overrideTransition) {
				sm->ChangeScene(destination, kCovers[coverIndex].type, kReveals[revealIndex].type);
			} else {
				sm->ChangeScene(destination);
			}
		}
		ImGui::EndDisabled();
	}
#endif // _DEBUG
}

Game::Game() = default;

Game::~Game() = default;

ISceneRunner* Game::GetSceneRunner() {
	// シーン駆動の実体はゲームの SceneManager。Framework はこの IF 経由で回す。
	return SceneManager::GetInstance();
}

#ifdef _DEBUG
void Game::RegisterGameEditorWindows() {
	auto& imgui = *ImGuiManager::GetInstance();

	// Hierarchy / Inspector / SceneEditor はフック経由でエンジン側に移設済み。
	// ゲーム固有の欄だけをセクションとして差し込む。
	GameInspectorSections::Register();      // Inspector: HP / ダメージ / 弾 / 近接 / プレハブ保存
	GameAssetBrowserSections::Register();   // SceneEditor: プレハブ一覧 / スプライン追加

	imgui.AddCallbackWindow("Transition",
		[]() { TransitionManager::GetInstance()->OnImGui(); });

	imgui.AddCallbackWindow("Hub Tuning",
		[]() {
			auto* sm = SceneManager::GetInstance();
			auto* scene = sm ? sm->GetCurrentScene() : nullptr;
			if (auto* hub = dynamic_cast<HubScene*>(scene)) {
				hub->OnImGuiTuning();
			} else {
				ImGui::TextDisabled("Active only in HUB scene.");
			}
		});

	imgui.AddCallbackWindow("StagePlay Tuning",
		[]() {
			auto* sm = SceneManager::GetInstance();
			auto* scene = sm ? sm->GetCurrentScene() : nullptr;
			if (auto* stage = dynamic_cast<StagePlayScene*>(scene)) {
				stage->OnImGuiTuning();
			} else {
				ImGui::TextDisabled("Active only in StagePlay scene.");
			}
		});

	imgui.AddCallbackWindow("Collision",
		[]() {
			auto* cm = CollisionManager::GetInstance();
			bool drawDebug = cm->IsDrawDebugEnabled();
			if (ImGui::Checkbox("Draw Colliders", &drawDebug)) {
				cm->SetDrawDebugEnabled(drawDebug);
			}
			ImGui::TextDisabled("- Tag-colored when not colliding");
			ImGui::TextDisabled("- Red when colliding this frame");
		});
}
#endif // _DEBUG

void Game::Initialize() {
	// エンティティ生成/破棄フックを配線（依存性の逆転）。
	// 以降に生成される全 IImGuiEditable はここで登録した処理を通る。
	IImGuiEditable::SetHooks(
		[](IImGuiEditable* e) {
			Gameplay::Of(e); // ゲームプレイコンポーネントのエントリを確保
			ImGuiManager::GetInstance()->Register(e);
			CollisionManager::GetInstance()->Register(e);
		},
		[](IImGuiEditable* e) {
			CollisionManager::GetInstance()->Unregister(e);
			ImGuiManager::GetInstance()->Unregister(e);
			Gameplay::Remove(e);
		});

	// エディタ核（エンジン）がゲームの実体へ触るためのフックを配線（依存性の逆転）。
	// これによりエンジン側は SceneManager / Game を名指ししない。
	{
		EditorHostHooks hooks{};
		hooks.getActiveScene = []() -> Scene* {
			auto* sm = SceneManager::GetInstance();
			return sm ? sm->GetCurrentScene() : nullptr;
		};
		hooks.getActiveSceneName = []() -> const char* {
			auto* sm = SceneManager::GetInstance();
			return sm ? sm->GetCurrentSceneName().c_str() : nullptr;
		};
		hooks.getPostEffect = []() -> PostEffect* { return Game::GetPostEffect(); };
		hooks.getFramework = [this]() -> Framework* { return this; };

		// Hierarchy のグルーピング / Inspector のグループ切替に EntityTag を使う。
		// エンジンはこれを「ただの整数」としてしか見ない。
		hooks.getEntityGroupCount = []() { return static_cast<int>(EntityTag::Count); };
		hooks.getEntityGroup = [](IImGuiEditable* e) {
			return static_cast<int>(Gameplay::Of(e).GetTag());
		};
		hooks.getEntityGroupName = [](int group) -> const char* {
			// GetTagName は string_view を返すが、実体は静的な文字列リテラルなので寿命は安全
			return GetTagName(static_cast<EntityTag>(group)).data();
		};
		hooks.getEntityGroupColor = [](int group, float& r, float& g, float& b, float& a) {
			GetTagColor(static_cast<EntityTag>(group), r, g, b, a);
		};
		hooks.setEntityGroup = [](IImGuiEditable* e, int group) {
			Gameplay::Of(e).SetTag(static_cast<EntityTag>(group));
		};

		// コライダーの実体はゲームの Gameplay サイドテーブルにある
		hooks.getCollider = [](IImGuiEditable* e) -> Collider* {
			return &Gameplay::Of(e).GetCollider();
		};

#ifdef _DEBUG
		hooks.drawSceneSwitcher = &DrawSceneSwitcher;
#endif

		ImGuiManager::SetHostHooks(hooks);
	}

	// 基底クラスの初期化処理
	Framework::Initialize();

	// 描画解像度（3D・ポストエフェクトの RT と深度）。UI は 1600×900 の仮想座標のまま、この RT に拡大して描かれる。
	// 既定はフル HD。--render-res <幅>x<高さ> で変えられる（16:9 に丸める）。
	// シーンや PostEffect を作る前にここで確定させる（後から変えるときは RequestRenderResolution → Game::Update で作り直し）
	{
		int32_t renderWidth = kDefaultRenderWidth;
		int32_t renderHeight = kDefaultRenderHeight;
		int argc = 0;
		LPWSTR* argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
		if (argv) {
			for (int i = 1; i + 1 < argc; ++i) {
				if (std::wcscmp(argv[i], L"--render-res") != 0) continue;
				// "<幅>x<高さ>" を読む
				wchar_t* end = nullptr;
				const long w = std::wcstol(argv[i + 1], &end, kDecimalRadix);
				if (end && (*end == L'x' || *end == L'X')) {
					const long h = std::wcstol(end + 1, nullptr, kDecimalRadix);
					if (w > 0 && h > 0) {
						renderWidth = static_cast<int32_t>(w);
						renderHeight = static_cast<int32_t>(h);
					}
				}
			}
			::LocalFree(argv);
		}
		GetDxCore()->RequestRenderResolution(renderWidth, renderHeight);
		GetDxCore()->ApplyPendingRenderResolution();
	}

#ifdef _DEBUG
	// エンジン標準パネルの生成後に、ゲーム対応のウィンドウ／パネルを追加登録する。
	// （エンジンから見えないゲーム型に触るものはすべてここに集約する）
	RegisterGameEditorWindows();
#endif

	// 初期シーンと PostEffect のロードを DStorage バッチで囲む。
	// (シーン内の全テクスチャ/メッシュ Enqueue を蓄積 → 末尾で 1 回だけ Wait)
	DStorageManager::GetInstance()->BeginBatch();

	//===================================
	// キーコンフィグの適用（InputActionMap への流し込み）
	// シーン作成前に確定させておく。user → default → 組み込みデフォルトの優先順位。
	//===================================
	if (auto* actionMap = GetInput()->GetActionMap()) {
		KeyConfig::LoadAndApply(*actionMap, keyConfigOptions_);
	}

	// サウンドを起動時に一括ロード（SE は自作合成 / BGM は魔王魂）
	LoadGameSounds();

	//===================================
	// 全シーン共通の GPU パーティクル + EffectManager を先に初期化。
	// シーン側はカメラセット + Update/Draw を呼ぶだけで済む。
	//===================================
	gpuParticleManager_ = std::make_unique<GPUParticleManager>();
	gpuParticleManager_->Initialize(GetDxCore(), GetSRVManager());
	gpuParticleManager_->CreateGroup("spark", "Resources/Textures/circle.dds");

	EffectManager::GetInstance()->Initialize(gpuParticleManager_.get());
	EffectManager::GetInstance()->LoadAllDefsInDirectory("Resources/Json/Effects");

#ifdef _DEBUG
	ImGuiManager::GetInstance()->SetGPUParticleManager(gpuParticleManager_.get());
#endif

	// プリファブ一覧を先に取り込む（シーン Initialize で InstantiatePrefab を使えるように）
	PrefabManager::GetInstance()->Rescan();

	//===================================
	// シーンファクトリを生成し、マネージャにセット
	//===================================
	SetSceneFactory(std::make_unique<SceneFactory>());
	SceneManager::GetInstance()->SetSceneFactory(GetSceneFactory());

	// プレイの状態を作り、最初のシーンを作る前に SceneManager へ渡す
	gameSession_ = std::make_unique<GameSession>();
	SceneManager::GetInstance()->SetGameSession(gameSession_.get());

	// シーンマネージャに最初のシーンをセット
	{
#if defined(_DEBUG) || defined(USE_PEPPER)
		// Debug と Development（NDEBUG＋USE_PEPPER）は StagePlay から（HUB の降下の先読みを通らないので、
		// 区間ごとのストリーミングのロードの重さをそのまま測れる。11 番 L9）
		std::string startScene = "STAGEPLAY";
#else
		std::string startScene = "TITLE";
#endif
		// --start-scene <名前>：最初のシーンを指定（計測用。tools/Python/run_cloud_bench.py が使う）
		int argc = 0;
		LPWSTR* argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
		if (argv) {
			for (int i = 1; i + 1 < argc; ++i) {
				if (std::wcscmp(argv[i], L"--start-scene") == 0) {
					startScene.clear();
					for (const wchar_t* c = argv[i + 1]; *c; ++c) startScene.push_back(static_cast<char>(*c));
				}
			}
			// --fullscreen：枠なしフルスクリーンで起動（展示用）。起動後も Alt+Enter / F11 で切り替えられる
			for (int i = 1; i < argc; ++i) {
				if (std::wcscmp(argv[i], L"--fullscreen") == 0) GetWinApp()->SetFullscreen(true);
			}
			::LocalFree(argv);
		}
		SceneManager::GetInstance()->ChangeSceneImmediate(startScene);
	}

	// PostEffect初期化（RenderTextureも内部で作成される）
	postEffect_ = std::make_unique<PostEffect>();
	postEffect_->Initialize(GetDxCore(), GetSRVManager(),
		GetDxCore()->GetRenderWidth(),
		GetDxCore()->GetRenderHeight());

	// バッチ終了 - DStorage の全リクエストをここで 1 回だけ Submit + Wait
	DStorageManager::GetInstance()->EndBatchAndWait();

#ifdef _DEBUG
	// Debug ビルド: PostEffect 適用後の最終出力先として、Viewport表示専用 RenderTexture を作成
	// クリアカラーのアルファ=1.0 にして、エフェクトOFF/各シーン端でImGui背景が透けないようにする
	viewportRenderTexture_ = std::make_unique<RenderTexture>();
	// シーンの空きをわかりやすくする水色背景（透明だとPostEffect結果と区別しづらいため）
	const float viewportClearColor[4] = { 0.1f, 0.25f, 0.5f, 1.0f };
	viewportRenderTexture_->Initialize(GetDxCore(), GetSRVManager(),
		GetDxCore()->GetRenderWidth(),
		GetDxCore()->GetRenderHeight(),
		DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
		viewportClearColor);

	// ViewportWindow にはエフェクト適用後の最終出力 RenderTexture を渡す
	ImGuiManager::GetInstance()->SetViewportRenderTexture(viewportRenderTexture_.get());
#endif
}

void Game::Update() {
	// 描画解像度の変更（オプション画面などの RequestRenderResolution）をフレームの頭で反映する。
	// 深度は DirectXCore が作り直すので、シーン RT など Game が持つ物をここで合わせる。
	// 水面の反射・トランジションの水面など、シーンが持つ RT は描くときに自分で合わせる
	if (GetDxCore()->ApplyPendingRenderResolution()) {
		const uint32_t width = static_cast<uint32_t>(GetDxCore()->GetRenderWidth());
		const uint32_t height = static_cast<uint32_t>(GetDxCore()->GetRenderHeight());
		if (postEffect_) postEffect_->Resize(width, height);
#ifdef _DEBUG
		if (viewportRenderTexture_) viewportRenderTexture_->Resize(width, height);
#endif
	}

	// 基底クラスの更新処理
	Framework::Update();

	// 終了リクエストが来ていたら処理しない
	if (IsEndRequest()) {
		return;
	}

	// F8：デモモード（展示用の自動進行）の開始・終了。どのシーンからでも切り替えられる
	if (GetInput() && GetInput()->GetKeyboard() && GetInput()->GetKeyboard()->TriggerKey(DIK_F8)) {
		AttractMode::GetInstance()->Toggle();
	}
}

void Game::Draw() {
	PEPPER_SCOPE("Framework::Draw");

	// 文字のインスタンスバッファの書き込み位置を先頭へ（同じフレームの Flush は続きへ書く）
	TextRenderer::GetInstance()->BeginFrame();

	// 1 フレームの描画パス。並び順に意味がある（理由は各関数の先頭のコメント）
	//   影の深度 → シーン → 強調表示のマスク → 歪み → 発光 → ポストエフェクトで合成し、合成後に重ねる物 → ImGui
	RenderShadowPass();
	RenderScenePass();
	RenderIdMaskPass();
	RenderDistortionPass();
	RenderBloomPass();
	RenderPostEffectAndOverlay();

#ifdef _DEBUG
	// ImGui は Swapchain に直接描く
	ImGuiManager::GetInstance()->EndFrame();
#endif // _DEBUG

	GetDxCore()->EndDraw();
	// シーン切り替え後の最初のフレームが画面に出た時点（Present＋GPU完了）でロード計測を出力
	SceneManager::GetInstance()->OnFramePresented();
	GetDxCore()->TickIntermediateResources();
	GetDxCore()->TickPendingCallbacks();
}

void Game::BindRenderSizeTarget(D3D12_CPU_DESCRIPTOR_HANDLE rtv, const D3D12_CPU_DESCRIPTOR_HANDLE* dsv) {
	auto* cmd = GetDxCore()->GetCommandList();
	cmd->OMSetRenderTargets(1, &rtv, false, dsv);

	const float width = static_cast<float>(GetDxCore()->GetRenderWidth());
	const float height = static_cast<float>(GetDxCore()->GetRenderHeight());
	D3D12_VIEWPORT viewport{ 0.0f, 0.0f, width, height, 0.0f, 1.0f };
	D3D12_RECT scissor{ 0, 0, static_cast<LONG>(width), static_cast<LONG>(height) };
	cmd->RSSetViewports(1, &viewport);
	cmd->RSSetScissorRects(1, &scissor);
}

// 影の深度（CSM）。シーンを描く前に、平行光源から見た深度を書いておく。
// 受光側のリソース(b5/t3)は Object3DManager に渡し、各シーンの DrawSetting でバインドされる。
void Game::RenderShadowPass() {
	if (!GetShadowMap()) return;
	PEPPER_SCOPE("Game::ShadowPass");

	if (Scene* scene = SceneManager::GetInstance()->GetCurrentScene()) {
		if (Camera* cam = scene->GetCamera()) {
			auto* dl = LightManager::GetInstance()->GetDirectionalLightData();
			// 無効時もカスケード更新（enabled フラグを CB に反映）するが、深度描画は省略
			GetShadowMap()->UpdateCascades(*cam, dl->direction);

			if (GetShadowMap()->IsEnabled()) {
				auto* cmd = GetDxCore()->GetCommandList();
				// スキニングモデルにも影を落とさせるため、影の前にスキニングを確定する。
				// Compute は SRV ヒープを使うので先に PreDraw でヒープを束ねる。
				GetSRVManager()->PreDraw();
				scene->DispatchDynamicAnimatedSkinning();

				// 深度を書く所だけ GPU で測る（スキニングの Compute は含めない）
				PEPPER_GPU_SCOPE(cmd, "Game::ShadowPass");
				GetShadowMap()->BeginPass(cmd);
				for (uint32_t c = 0; c < kShadowCascadeCount; ++c) {
					GetShadowMap()->BindCascade(cmd, c);
					scene->DrawShadowCasters();
				}
				GetShadowMap()->EndPass(cmd);
			}
		}
	}
	GetObject3DManager()->SetShadowBindings(
		GetShadowMap()->GetConstantsGpuAddress(), GetShadowMap()->GetSrvGpuHandle());
}

// シーン本体を PostEffect の RenderTexture に描く。
// 水面反射の RT はシーンの Draw の中で、水面を描く前に作られる（WaterReflection::Render）。
void Game::RenderScenePass() {
	auto* cmd = GetDxCore()->GetCommandList();
	D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = GetDxCore()->GetDsvHandle();

	postEffect_->BeginSceneRender(cmd, &dsvHandle);
	GetSRVManager()->PreDraw();
	cmd->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

	SceneManager::GetInstance()->Draw();

	// デモモードの表示：どのシーンでも、トランジションの上に出す。
	// トランジションが RT を切り替えていることがあるので、シーンの RT とビューポートを付け直してから描く
	if (AttractMode::GetInstance()->IsEnabled()) {
		BindRenderSizeTarget(postEffect_->GetSceneRenderTarget()->GetRTVHandle(), nullptr);
		AttractMode::GetInstance()->DrawLabel();
	}

	postEffect_->EndSceneRender(cmd);
}

// 強調表示（アウトライン）の対象を idMaskRT に書く。シーンの深度を使って隠れた所は書かない。
void Game::RenderIdMaskPass() {
	Scene* scene = SceneManager::GetInstance()->GetCurrentScene();
	if (!scene) return;
	PEPPER_SCOPE("Game::IdPass");

	auto* cmd = GetDxCore()->GetCommandList();
	postEffect_->BeginIdPass(cmd);
	// 対象が無いフレームも、0 でクリアして SRV 状態に遷移だけはしておく（PostEffect が毎フレーム読むため）
	if (!scene->GetHighlights().empty() || scene->HasExtraIdPassObjects()) {
		const auto dsv = GetDxCore()->GetDsvHandle();
		BindRenderSizeTarget(postEffect_->GetIdMaskRT()->GetRTVHandle(), &dsv);
		GetSRVManager()->PreDraw();
		scene->RunIdPass(cmd);
	}
	postEffect_->EndIdPass(cmd);
}

// エフェクトの useDistortion なプリミティブを distortionRT に書く（深度テストのみ・書き込みなし）。
// 歪み源が無いフレームはパス全体と PostEffect の Distortion 合成を丸ごと飛ばして GPU を節約する。
void Game::RenderDistortionPass() {
	const bool distortionActive = EffectManager::GetInstance()->HasActiveDistortionSource();
	if (postEffect_ && postEffect_->distortion) {
		postEffect_->distortion->SetEnabled(distortionActive);
	}
	if (!distortionActive) return;
	PEPPER_SCOPE("Game::DistortionPass");

	auto* cmd = GetDxCore()->GetCommandList();
	postEffect_->BeginDistortionPass(cmd);
	const auto dsv = GetDxCore()->GetDsvHandle();
	BindRenderSizeTarget(postEffect_->GetDistortionRT()->GetRTVHandle(), &dsv);
	GetSRVManager()->PreDraw();
	EffectManager::GetInstance()->DrawDistortionPass();
	postEffect_->EndDistortionPass(cmd);
}

// 光らせる物だけを発光 RT（bloomRT）に書く。対象はエフェクトの useBloom、シーン自身の発光（コクピットなど）、
// 文字の発光の層（TextRenderer）。発光源が無いフレームはパスも PostEffect の Bloom 合成も飛ばす。
void Game::RenderBloomPass() {
	Scene* scene = SceneManager::GetInstance()->GetCurrentScene();
	const bool effectBloom = EffectManager::GetInstance()->HasActiveBloomSource();
	const bool sceneBloom = scene && scene->HasBloomSources();
	const bool textBloom = TextRenderer::GetInstance()->HasPendingGlow();

	// トランジションが画面を覆う間はブルームを消す（覆いはシーンの絵の中にあり、光が覆いの上に残るため）
	{
		const float target = SceneManager::GetInstance()->IsTransitionCovering() ? 0.0f : 1.0f;
		const float step = (std::min)(GetDxCore()->GetDeltaTime(), 1.0f / kBloomFadeMinFps) / kBloomFadeSeconds;
		bloomFade_ = (bloomFade_ < target) ? (std::min)(bloomFade_ + step, target) : (std::max)(bloomFade_ - step, target);
	}
	const bool bloomActive = (effectBloom || sceneBloom || textBloom) && bloomFade_ > 0.0f;
	if (postEffect_ && postEffect_->bloom) {
		postEffect_->bloom->SetEnabled(bloomActive);
		postEffect_->bloom->SetFade(bloomFade_);
	}
	if (!bloomActive) return;
	PEPPER_SCOPE("Game::BloomPass");

	auto* cmd = GetDxCore()->GetCommandList();
	// RTV＋ビューポートは BeginBloomPass が設定する。深度テスト用に DSV を渡す
	auto dsv = GetDxCore()->GetDsvHandle();
	postEffect_->BeginBloomPass(cmd, &dsv);
	GetSRVManager()->PreDraw();
	if (effectBloom) EffectManager::GetInstance()->DrawBloomPass();
	if (sceneBloom) scene->DrawBloomSources();
	if (textBloom) TextRenderer::GetInstance()->FlushGlow();
	postEffect_->EndBloomPass(cmd);
}

// ここまでの RT をポストエフェクトで合成し、その上に「合成後に重ねる物」を描く。
// 重ねる物（必殺技の崩壊の破片など）は、合成より前に描くと色反転が二重にかかって元の色に戻るため、必ず合成の後。
// Release：Swapchain へ直接出力 / Debug：ImGui のビューポートに出す RenderTexture へ出力
void Game::RenderPostEffectAndOverlay() {
	GetDxCore()->BeginDraw();
	GetSRVManager()->PreDraw();

	// Outline系エフェクトに最新の射影行列を渡す
	if (Camera* cam = GetObject3DManager()->GetDefaultCamera()) {
		postEffect_->SetProjectionMatrix(cam->GetProjectionMatrix());
	}

	auto* cmd = GetDxCore()->GetCommandList();
	Scene* scene = SceneManager::GetInstance()->GetCurrentScene();
#ifdef _DEBUG
	{ PEPPER_SCOPE("Game::PostEffectDraw"); postEffect_->Draw(cmd, viewportRenderTexture_.get()); }
	if (scene) {
		DrawOverlayOnViewportTexture(scene);
	}
	// Effect Editor のプレビュー RT への描画。どのシーンでも動くよう、シーンに任せずここで描く。
	// 共有の深度バッファをクリアして使うので、シーンの深度を読む処理（ID パス・ポストエフェクト）が
	// すべて終わったこの位置で行う。エディタを開いていなければ中で何もしない
	if (auto* effectEditor = ImGuiManager::GetInstance()->GetEffectEditorWindow()) {
		GetSRVManager()->PreDraw();
		effectEditor->Render();
	}
	// ImGui を Swapchain に描画するために RTV を戻す
	GetDxCore()->RestoreSwapchainRenderTarget(cmd);
	GetSRVManager()->PreDraw();
#else
	{ PEPPER_SCOPE("Game::PostEffectDraw"); postEffect_->Draw(cmd); }
	// Swapchain の RTV は DSV なしでバインド済み
	if (scene) {
		GetSRVManager()->PreDraw();
		scene->DrawAfterPostEffect(cmd);
	}
#endif
}

#ifdef _DEBUG
// 合成後に重ねる物を viewportRT に描く。viewportRT は合成の後 SRV 状態なので、
// 合成した絵を消さないよう手動バリアで RT にして（クリアしない・DSV なし）描き、SRV に戻す（ImGui::Image で読むため）
void Game::DrawOverlayOnViewportTexture(Scene* scene) {
	auto* cmd = GetDxCore()->GetCommandList();
	RenderTexture* vrt = viewportRenderTexture_.get();

	D3D12_RESOURCE_BARRIER barrier{};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Transition.pResource = vrt->GetResource();
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	cmd->ResourceBarrier(1, &barrier);

	auto rtv = vrt->GetRTVHandle();
	cmd->OMSetRenderTargets(1, &rtv, false, nullptr);
	D3D12_VIEWPORT vp{ 0.0f, 0.0f,
		static_cast<float>(vrt->GetWidth()), static_cast<float>(vrt->GetHeight()), 0.0f, 1.0f };
	D3D12_RECT sc{ 0, 0,
		static_cast<LONG>(vrt->GetWidth()), static_cast<LONG>(vrt->GetHeight()) };
	cmd->RSSetViewports(1, &vp);
	cmd->RSSetScissorRects(1, &sc);
	GetSRVManager()->PreDraw();
	scene->DrawAfterPostEffect(cmd);

	std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
	cmd->ResourceBarrier(1, &barrier);
}
#endif

void Game::Finalize() {
#ifdef _DEBUG
	ImGuiManager::GetInstance()->SetGPUParticleManager(nullptr);
#endif

	//===================================
	// 全シーン共通のエフェクト系を GPU 停止前に解放
	//===================================
	EffectManager::GetInstance()->Finalize();

	if (gpuParticleManager_) {
		gpuParticleManager_->Finalize();
		gpuParticleManager_.reset();
	}

	//===================================
	// PostEffectの終了処理
	//===================================
	if (postEffect_) {
		postEffect_->Finalize();
	}

#ifdef _DEBUG
	if (viewportRenderTexture_) {
		viewportRenderTexture_->Finalize();
	}
#endif

	//===================================
	// 基底クラスの終了処理
	//===================================
	Framework::Finalize();
}
