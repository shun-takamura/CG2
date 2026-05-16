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
#include "CameraCapture.h"
#include "QRCodeReader.h"
#include "TransitionManager.h"
#include "Camera.h"
#include "DStorageManager.h"
#include "InputAction.h"
#include "Config/KeyConfig.h"
#include <memory>

std::unique_ptr<PostEffect> Game::postEffect_ = nullptr;
Game* Game::instance_ = nullptr;

Game::Game() {
	instance_ = this;
}

Game::~Game() {
	instance_ = nullptr;
}

void Game::Initialize() {
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
	// シーンファクトリを生成し、マネージャにセット
	//===================================
	sceneFactory_ = std::make_unique<SceneFactory>();
	SceneManager::GetInstance()->SetSceneFactory(sceneFactory_.get());

	// シーンマネージャに最初のシーンをセット
#ifdef _DEBUG
	SceneManager::GetInstance()->ChangeSceneImmediate("DEMO");
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
	const float viewportClearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
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
#ifdef _DEBUG
	postEffect_->Draw(dxCore_->GetCommandList(), viewportRenderTexture_.get());
	// PostEffect::Draw 内で RTV が viewportRenderTexture_ に切り替えられたので、
	// ImGui を Swapchain に描画するために RTV を戻す
	dxCore_->RestoreSwapchainRenderTarget(dxCore_->GetCommandList());
	srvManager_->PreDraw();
#else
	postEffect_->Draw(dxCore_->GetCommandList());
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
