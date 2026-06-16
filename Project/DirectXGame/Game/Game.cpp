#include "Game.h"

#include "DirectXTex.h"
#include "d3dx12.h"

//============================
// 自作ヘッダーのインクルード
//============================
#include "WindowsApplication.h"
#include "DirectXCore.h"
#include "SpriteManager.h"
#include "Object3DManager.h"
#include "SRVManager.h"
#include "InputManager.h"
#include "ImGuiManager.h"
#include "KeyboardInput.h"
#include "SceneManager.h"
#include "SceneFactory.h"
#include "IImGuiEditable.h"
#include "Components/Gameplay.h"
#include "Components/CollisionManager.h"
#include "CameraCapture.h"
#include "QRCodeReader.h"
#include "TransitionManager.h"
#include "Camera.h"
#include "DStorageManager.h"
#include "InputAction.h"
#include "Config/KeyConfig.h"
#include "GPUParticleManager.h"
#include "Effect/EffectManager.h"
#include "Components/PrefabManager.h"
#include "Scene/Scene.h"
#include "LightManager.h"
#include "PepperMacros.h"
#include <memory>

std::unique_ptr<PostEffect> Game::postEffect_ = nullptr;
std::unique_ptr<GPUParticleManager> Game::gpuParticleManager_ = nullptr;
Game* Game::instance_ = nullptr;

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

	// 基底クラスの初期化処理
	Framework::Initialize();

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
#ifdef _DEBUG
	SceneManager::GetInstance()->ChangeSceneImmediate("STAGEPLAY");
#else
	SceneManager::GetInstance()->ChangeSceneImmediate("TITLE");
#endif

	// PostEffect初期化（RenderTextureも内部で作成される）
	postEffect_ = std::make_unique<PostEffect>();
	postEffect_->Initialize(dxCore_.get(), srvManager_.get(),
		WindowsApplication::kClientWidth,
		WindowsApplication::kClientHeight);

	// バッチ終了 - DStorage の全リクエストをここで 1 回だけ Submit + Wait
	DStorageManager::GetInstance()->EndBatchAndWait();

#ifdef _DEBUG
	// Debug ビルド: PostEffect 適用後の最終出力先として、Viewport表示専用 RenderTexture を作成
	// クリアカラーのアルファ=1.0 にして、エフェクトOFF/各シーン端でImGui背景が透けないようにする
	viewportRenderTexture_ = std::make_unique<RenderTexture>();
	// シーンの空きをわかりやすくする水色背景（透明だとPostEffect結果と区別しづらいため）
	const float viewportClearColor[4] = { 0.1f, 0.25f, 0.5f, 1.0f };
	viewportRenderTexture_->Initialize(dxCore_.get(), srvManager_.get(),
		WindowsApplication::kClientWidth,
		WindowsApplication::kClientHeight,
		DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
		viewportClearColor);

	// ViewportWindow にはエフェクト適用後の最終出力 RenderTexture を渡す
	ImGuiManager::Instance().SetViewportRenderTexture(viewportRenderTexture_.get());
#endif
}

void Game::Update() {
	// 基底クラスの更新処理
	Framework::Update();

	// 終了リクエストが来ていたら処理しない
	if (endRequest_) {
		return;
	}
}

void Game::Draw() {
	PEPPER_SCOPE("Framework::Draw");

	// 0. シャドウパス（CSM）：シーンRT描画の前に、平行光源視点で深度を書く。
	//    受光リソース(b5/t3)は Object3DManager に渡し、各シーンの DrawSetting でバインドされる。
	if (shadowMap_) {
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

	postEffect_->EndSceneRender(dxCore_->GetCommandList());

	// ----- ID Pass：ハイライト対象を idMaskRT に書き込む -----
	if (Scene* scene = SceneManager::GetInstance()->GetCurrentScene()) {
		if (!scene->GetHighlights().empty()) {
			auto* cmd = dxCore_->GetCommandList();
			postEffect_->BeginIdPass(cmd);

			// RTV + DSV をバインド
			auto rtv = postEffect_->GetIdMaskRT()->GetRTVHandle();
			auto dsv = dxCore_->GetDsvHandle();
			cmd->OMSetRenderTargets(1, &rtv, false, &dsv);

			D3D12_VIEWPORT vp{};
			vp.Width = static_cast<float>(WindowsApplication::kClientWidth);
			vp.Height = static_cast<float>(WindowsApplication::kClientHeight);
			vp.MaxDepth = 1.0f;
			cmd->RSSetViewports(1, &vp);
			D3D12_RECT sc{ 0, 0,
				static_cast<LONG>(WindowsApplication::kClientWidth),
				static_cast<LONG>(WindowsApplication::kClientHeight) };
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
		auto* cmd = dxCore_->GetCommandList();
		postEffect_->BeginDistortionPass(cmd);

		// RTV + DSV をバインド（IdPass と同じ要領、深度テストのみ・書き込みなし）
		auto rtv = postEffect_->GetDistortionRT()->GetRTVHandle();
		auto dsv = dxCore_->GetDsvHandle();
		cmd->OMSetRenderTargets(1, &rtv, false, &dsv);

		D3D12_VIEWPORT vp{};
		vp.Width = static_cast<float>(WindowsApplication::kClientWidth);
		vp.Height = static_cast<float>(WindowsApplication::kClientHeight);
		vp.MaxDepth = 1.0f;
		cmd->RSSetViewports(1, &vp);
		D3D12_RECT sc{ 0, 0,
			static_cast<LONG>(WindowsApplication::kClientWidth),
			static_cast<LONG>(WindowsApplication::kClientHeight) };
		cmd->RSSetScissorRects(1, &sc);

		srvManager_->PreDraw();
		EffectManager::GetInstance()->DrawDistortionPass();

		postEffect_->EndDistortionPass(cmd);
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
	postEffect_->Draw(cmd, viewportRenderTexture_.get());
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
	// ImGui を Swapchain に描画するために RTV を戻す
	dxCore_->RestoreSwapchainRenderTarget(cmd);
	srvManager_->PreDraw();
#else
	postEffect_->Draw(cmd);
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
