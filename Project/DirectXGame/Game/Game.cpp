#include "Game.h"
#include "SoundManager.h"
#include <filesystem>

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
#include "Scene/Scene.h"
#include "LightManager.h"
#include "PepperMacros.h"
#include <memory>

std::unique_ptr<PostEffect> Game::postEffect_ = nullptr;
std::unique_ptr<GPUParticleManager> Game::gpuParticleManager_ = nullptr;
Game* Game::instance_ = nullptr;

namespace {
	// SoundManager::LoadFile はファイルが無いと assert で落ちるので、存在するものだけ読む
	// （BGM を配置する前でも起動できる。鳴らす側は未ロード名なら何もしない）。
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
	// TimeControler の「Scene Switch」欄（EditorHostHooks::drawSceneSwitcher）。
	// 行き先を選んで、ゲームと同じ組み合わせ（SceneRoutes）か、手で選んだトランジションで切り替える。
	// STAGEPLAY からはクリア / 失敗を選ぶと、ゲームが実際に使う行き先（RESULT / GAMEOVER）になる。
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

Game::Game() {
	instance_ = this;
}

Game::~Game() {
	instance_ = nullptr;
}

ISceneRunner* Game::GetSceneRunner() {
	// シーン駆動の実体はゲームの SceneManager。Framework はこの IF 経由で回す。
	return SceneManager::GetInstance();
}

#ifdef _DEBUG
void Game::RegisterGameEditorWindows() {
	auto& imgui = ImGuiManager::Instance();

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
			ImGuiManager::Instance().Register(e);
			CollisionManager::GetInstance()->Register(e);
		},
		[](IImGuiEditable* e) {
			CollisionManager::GetInstance()->Unregister(e);
			ImGuiManager::Instance().Unregister(e);
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
		hooks.getFramework = []() -> Framework* { return Game::GetInstance(); };

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
				const long w = std::wcstol(argv[i + 1], &end, 10);
				if (end && (*end == L'x' || *end == L'X')) {
					const long h = std::wcstol(end + 1, nullptr, 10);
					if (w > 0 && h > 0) {
						renderWidth = static_cast<int32_t>(w);
						renderHeight = static_cast<int32_t>(h);
					}
				}
			}
			::LocalFree(argv);
		}
		dxCore_->RequestRenderResolution(renderWidth, renderHeight);
		dxCore_->ApplyPendingRenderResolution();
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
	if (auto* actionMap = input_->GetActionMap()) {
		KeyConfig::LoadAndApply(*actionMap, keyConfigOptions_);
	}

	// サウンドを起動時に一括ロード（SE は自作合成 / BGM は魔王魂）
	LoadGameSounds();

	//===================================
	// 全シーン共通の GPU パーティクル + EffectManager を先に初期化。
	// シーン側はカメラセット + Update/Draw を呼ぶだけで済む。
	//===================================
	gpuParticleManager_ = std::make_unique<GPUParticleManager>();
	gpuParticleManager_->Initialize(dxCore_.get(), srvManager_.get());
	gpuParticleManager_->CreateGroup("spark", "Resources/Textures/circle.dds");

	EffectManager::GetInstance()->Initialize(gpuParticleManager_.get());
	EffectManager::GetInstance()->LoadAllDefsInDirectory("Resources/Json/Effects");

#ifdef _DEBUG
	ImGuiManager::Instance().SetGPUParticleManager(gpuParticleManager_.get());
#endif

	// プリファブ一覧を先に取り込む（シーン Initialize で InstantiatePrefab を使えるように）
	PrefabManager::GetInstance()->Rescan();

	//===================================
	// シーンファクトリを生成し、マネージャにセット
	//===================================
	sceneFactory_ = std::make_unique<SceneFactory>();
	SceneManager::GetInstance()->SetSceneFactory(sceneFactory_.get());

	// シーンマネージャに最初のシーンをセット
	{
#ifdef _DEBUG
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
				if (std::wcscmp(argv[i], L"--fullscreen") == 0) winApp_->SetFullscreen(true);
			}
			::LocalFree(argv);
		}
		SceneManager::GetInstance()->ChangeSceneImmediate(startScene);
	}

	// PostEffect初期化（RenderTextureも内部で作成される）
	postEffect_ = std::make_unique<PostEffect>();
	postEffect_->Initialize(dxCore_.get(), srvManager_.get(),
		dxCore_->GetRenderWidth(),
		dxCore_->GetRenderHeight());

	// バッチ終了 - DStorage の全リクエストをここで 1 回だけ Submit + Wait
	DStorageManager::GetInstance()->EndBatchAndWait();

#ifdef _DEBUG
	// Debug ビルド: PostEffect 適用後の最終出力先として、Viewport表示専用 RenderTexture を作成
	// クリアカラーのアルファ=1.0 にして、エフェクトOFF/各シーン端でImGui背景が透けないようにする
	viewportRenderTexture_ = std::make_unique<RenderTexture>();
	// シーンの空きをわかりやすくする水色背景（透明だとPostEffect結果と区別しづらいため）
	const float viewportClearColor[4] = { 0.1f, 0.25f, 0.5f, 1.0f };
	viewportRenderTexture_->Initialize(dxCore_.get(), srvManager_.get(),
		dxCore_->GetRenderWidth(),
		dxCore_->GetRenderHeight(),
		DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
		viewportClearColor);

	// ViewportWindow にはエフェクト適用後の最終出力 RenderTexture を渡す
	ImGuiManager::Instance().SetViewportRenderTexture(viewportRenderTexture_.get());
#endif
}

void Game::Update() {
	// 描画解像度の変更（オプション画面などの RequestRenderResolution）をフレームの頭で反映する。
	// 深度は DirectXCore が作り直すので、シーン RT など Game が持つ物をここで合わせる。
	// 水面の反射・トランジションの水面など、シーンが持つ RT は描くときに自分で合わせる
	if (dxCore_->ApplyPendingRenderResolution()) {
		const uint32_t width = static_cast<uint32_t>(dxCore_->GetRenderWidth());
		const uint32_t height = static_cast<uint32_t>(dxCore_->GetRenderHeight());
		if (postEffect_) postEffect_->Resize(width, height);
#ifdef _DEBUG
		if (viewportRenderTexture_) viewportRenderTexture_->Resize(width, height);
#endif
	}

	// 基底クラスの更新処理
	Framework::Update();

	// 終了リクエストが来ていたら処理しない
	if (endRequest_) {
		return;
	}

	// F8：デモモード（展示用の自動進行）の開始・終了。どのシーンからでも切り替えられる
	if (input_ && input_->GetKeyboard() && input_->GetKeyboard()->TriggerKey(DIK_F8)) {
		AttractMode::GetInstance()->Toggle();
	}
}

void Game::Draw() {
	PEPPER_SCOPE("Framework::Draw");

	// 文字のインスタンスバッファの書き込み位置を先頭へ（同じフレームの Flush は続きへ書く）
	TextRenderer::GetInstance()->BeginFrame();

	// 0. シャドウパス（CSM）：シーンRT描画の前に、平行光源視点で深度を書く。
	//    受光リソース(b5/t3)は Object3DManager に渡し、各シーンの DrawSetting でバインドされる。
	if (shadowMap_) {
		PEPPER_SCOPE("Game::ShadowPass");
		if (Scene* scene = SceneManager::GetInstance()->GetCurrentScene()) {
			if (Camera* cam = scene->GetCamera()) {
				auto* dl = LightManager::GetInstance()->GetDirectionalLightData();
				// 無効時もカスケード更新（enabled フラグを CB に反映）するが、深度描画は省略
				shadowMap_->UpdateCascades(*cam, dl->direction);

				if (shadowMap_->IsEnabled()) {
					auto* cmd = dxCore_->GetCommandList();
					// スキニングモデルをキャストさせるため、シャドウパス前にスキニングを確定する。
					// Compute は SRV ヒープを使うので先に PreDraw でヒープを束ねる。
					srvManager_->PreDraw();
					scene->DispatchDynamicAnimatedSkinning();

					shadowMap_->BeginPass(cmd);
					for (uint32_t c = 0; c < kShadowCascadeCount; ++c) {
						shadowMap_->BindCascade(cmd, c);
						scene->DrawShadowCasters();
					}
					shadowMap_->EndPass(cmd);
				}
			}
		}
		object3DManager_->SetShadowBindings(
			shadowMap_->GetConstantsGpuAddress(), shadowMap_->GetSrvGpuHandle());
	}

	// 1. PostEffectのRenderTextureにシーンを描画
	D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle =
		dxCore_->GetDsvHeap()->GetCPUDescriptorHandleForHeapStart();

	postEffect_->BeginSceneRender(dxCore_->GetCommandList(), &dsvHandle);

	srvManager_->PreDraw();

	// 深度クリア
	dxCore_->GetCommandList()->ClearDepthStencilView(
		dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

	// シーン描画
	SceneManager::GetInstance()->Draw();

	// デモモードの表示：どのシーンでも、トランジションの上に出す。
	// トランジションが RT を切り替えていることがあるので、シーンの RT とビューポートを付け直してから描く
	if (AttractMode::GetInstance()->IsEnabled()) {
		auto* cmd = dxCore_->GetCommandList();
		auto rtv = postEffect_->GetSceneRenderTarget()->GetRTVHandle();
		cmd->OMSetRenderTargets(1, &rtv, false, nullptr);
		const float width = static_cast<float>(dxCore_->GetRenderWidth());
		const float height = static_cast<float>(dxCore_->GetRenderHeight());
		D3D12_VIEWPORT viewport{ 0.0f, 0.0f, width, height, 0.0f, 1.0f };
		D3D12_RECT scissor{ 0, 0, static_cast<LONG>(width), static_cast<LONG>(height) };
		cmd->RSSetViewports(1, &viewport);
		cmd->RSSetScissorRects(1, &scissor);
		AttractMode::GetInstance()->DrawLabel();
	}

	postEffect_->EndSceneRender(dxCore_->GetCommandList());

	// ----- ID Pass：ハイライト対象を idMaskRT に書き込む -----
	if (Scene* scene = SceneManager::GetInstance()->GetCurrentScene()) {
		PEPPER_SCOPE("Game::IdPass");
		if (!scene->GetHighlights().empty() || scene->HasExtraIdPassObjects()) {
			auto* cmd = dxCore_->GetCommandList();
			postEffect_->BeginIdPass(cmd);

			// RTV + DSV をバインド
			auto rtv = postEffect_->GetIdMaskRT()->GetRTVHandle();
			auto dsv = dxCore_->GetDsvHandle();
			cmd->OMSetRenderTargets(1, &rtv, false, &dsv);

			D3D12_VIEWPORT vp{};
			vp.Width = static_cast<float>(dxCore_->GetRenderWidth());
			vp.Height = static_cast<float>(dxCore_->GetRenderHeight());
			vp.MaxDepth = 1.0f;
			cmd->RSSetViewports(1, &vp);
			D3D12_RECT sc{ 0, 0,
				static_cast<LONG>(dxCore_->GetRenderWidth()),
				static_cast<LONG>(dxCore_->GetRenderHeight()) };
			cmd->RSSetScissorRects(1, &sc);

			srvManager_->PreDraw();
			scene->RunIdPass(cmd);

			postEffect_->EndIdPass(cmd);
		} else {
			// ハイライト無し時も idMaskRT を SRV 可能状態に維持（0でクリアして遷移）
			auto* cmd = dxCore_->GetCommandList();
			postEffect_->BeginIdPass(cmd);
			postEffect_->EndIdPass(cmd);
		}
	}

	// ----- Distortion Pass：エフェクト中の useDistortion なプリミティブを distortionRT に書き込む -----
	// 歪み源が無いフレームはパス全体（RT 遷移・クリア・描画・PostEffect 合成）をスキップして GPU を節約する。
	// 合成側 (PostEffect の Distortion フィルタ) も同じフラグで毎フレーム自動 ON/OFF する。
	const bool distortionActive = EffectManager::GetInstance()->HasActiveDistortionSource();
	if (postEffect_ && postEffect_->distortion) {
		postEffect_->distortion->SetEnabled(distortionActive);
	}
	if (distortionActive) {
		PEPPER_SCOPE("Game::DistortionPass");
		auto* cmd = dxCore_->GetCommandList();
		postEffect_->BeginDistortionPass(cmd);

		// RTV + DSV をバインド（IdPass と同じ要領、深度テストのみ・書き込みなし）
		auto rtv = postEffect_->GetDistortionRT()->GetRTVHandle();
		auto dsv = dxCore_->GetDsvHandle();
		cmd->OMSetRenderTargets(1, &rtv, false, &dsv);

		D3D12_VIEWPORT vp{};
		vp.Width = static_cast<float>(dxCore_->GetRenderWidth());
		vp.Height = static_cast<float>(dxCore_->GetRenderHeight());
		vp.MaxDepth = 1.0f;
		cmd->RSSetViewports(1, &vp);
		D3D12_RECT sc{ 0, 0,
			static_cast<LONG>(dxCore_->GetRenderWidth()),
			static_cast<LONG>(dxCore_->GetRenderHeight()) };
		cmd->RSSetScissorRects(1, &sc);

		srvManager_->PreDraw();
		EffectManager::GetInstance()->DrawDistortionPass();

		postEffect_->EndDistortionPass(cmd);
	}

	// ----- Bloom Pass：useBloom なエフェクトだけを発光 RT（bloomRT）に書き込む -----
	// Distortion と同じく、発光源が無いフレームはパスも PostEffect の Bloom 合成も丸ごとスキップする。
	// シーンが自分で光らせる物（コクピットの発光など）と文字の発光の層（TextRenderer）も同じ発光 RT に描く
	Scene* bloomScene = SceneManager::GetInstance()->GetCurrentScene();
	const bool effectBloom = EffectManager::GetInstance()->HasActiveBloomSource();
	const bool sceneBloom = bloomScene && bloomScene->HasBloomSources();
	const bool textBloom = TextRenderer::GetInstance()->HasPendingGlow();
	const bool bloomActive = effectBloom || sceneBloom || textBloom;
	if (postEffect_ && postEffect_->bloom) {
		postEffect_->bloom->SetEnabled(bloomActive);
	}
	if (bloomActive) {
		PEPPER_SCOPE("Game::BloomPass");
		auto* cmd = dxCore_->GetCommandList();
		// RTV＋ビューポートは BeginBloomPass が設定する。深度テスト用に DSV を渡す
		auto dsv = dxCore_->GetDsvHandle();
		postEffect_->BeginBloomPass(cmd, &dsv);

		srvManager_->PreDraw();
		if (effectBloom) EffectManager::GetInstance()->DrawBloomPass();
		if (sceneBloom) bloomScene->DrawBloomSources();
		if (textBloom) TextRenderer::GetInstance()->FlushGlow();

		postEffect_->EndBloomPass(cmd);
	}

	// 2. Swapchainに切り替え
	dxCore_->BeginDraw();

	srvManager_->PreDraw();  // ディスクリプタヒープを再設定

	// Outline系エフェクトに最新の射影行列を渡す
	if (Camera* cam = object3DManager_->GetDefaultCamera()) {
		postEffect_->SetProjectionMatrix(cam->GetProjectionMatrix());
	}

	// 3. マルチパスでエフェクト適用
	// Releaseビルド: Swapchainへ直接出力
	// Debugビルド  : Viewport表示用 RenderTexture へ出力 → ImGui::Image で表示
	auto* cmd = dxCore_->GetCommandList();
	Scene* afterScene = SceneManager::GetInstance()->GetCurrentScene();
#ifdef _DEBUG
	{ PEPPER_SCOPE("Game::PostEffectDraw"); postEffect_->Draw(cmd, viewportRenderTexture_.get()); }
	// PostEffect 後のオーバーレイ（崩壊破片など）を viewportRT に重ねる。
	// viewportRT は Draw 後 SRV 状態なので、合成画像を消さないよう手動バリアで RT 化（クリアしない・DSVなし）。
	if (afterScene) {
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
		srvManager_->PreDraw();
		afterScene->DrawAfterPostEffect(cmd);

		// RT → SRV に戻す（ImGui::Image で読めるように）
		std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
		cmd->ResourceBarrier(1, &barrier);
	}
	// Effect Editor のプレビュー RT への描画。どのシーンでも動くよう、シーンに任せずここで描く。
	// 共有の深度バッファをクリアして使うので、シーンの深度を読む処理（ID パス・ポストエフェクト）が
	// すべて終わったこの位置で行う。エディタを開いていなければ中で何もしない
	if (auto* effectEditor = ImGuiManager::Instance().GetEffectEditorWindow()) {
		srvManager_->PreDraw();
		effectEditor->Render();
	}
	// ImGui を Swapchain に描画するために RTV を戻す
	dxCore_->RestoreSwapchainRenderTarget(cmd);
	srvManager_->PreDraw();
#else
	{ PEPPER_SCOPE("Game::PostEffectDraw"); postEffect_->Draw(cmd); }
	// PostEffect 後のオーバーレイ（崩壊破片など）。Swapchain RTV は DSVなしでバインド済み。
	if (afterScene) {
		srvManager_->PreDraw();
		afterScene->DrawAfterPostEffect(cmd);
	}
#endif

	// 4. ImGui描画（Swapchainに直接）
#ifdef _DEBUG
	ImGuiManager::Instance().EndFrame();
#endif // _DEBUG

	// 5. 終了
	dxCore_->EndDraw();
	// シーン切り替え後の最初のフレームが画面に出た時点（Present＋GPU完了）でロード計測を出力
	SceneManager::GetInstance()->OnFramePresented();
	dxCore_->TickIntermediateResources();
	dxCore_->TickPendingCallbacks();
}

void Game::Finalize() {
#ifdef _DEBUG
	ImGuiManager::Instance().SetGPUParticleManager(nullptr);
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
