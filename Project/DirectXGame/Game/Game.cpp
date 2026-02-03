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
#include"CameraCapture.h"
#include "QRCodeReader.h"
#include"TransitionManager.h"
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

	//===================================
	// シーンファクトリを生成し、マネージャにセット
	//===================================
	sceneFactory_ = std::make_unique<SceneFactory>();
	SceneManager::GetInstance()->SetSceneFactory(sceneFactory_.get());

	// シーンマネージャに最初のシーンをセット
	SceneManager::GetInstance()->ChangeSceneImmediate("GAMEPLAY");

	// クリアカラーを設定
	float redClearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };

	// RenderTexture初期化（画面サイズで作成）
	renderTexture_ = std::make_unique<RenderTexture>();
	renderTexture_->Initialize(dxCore_.get(), srvManager_.get(),
		WindowsApplication::kClientWidth,
		WindowsApplication::kClientHeight,
		DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
		redClearColor);

	// PostEffect初期化
	postEffect_ = std::make_unique<PostEffect>();
	postEffect_->Initialize(dxCore_.get(), srvManager_.get());
}

void Game::Update() {
	// 基底クラスの更新処理
	Framework::Update();

	// 終了リクエストが来ていたら処理しない
	if (endRequest_) {
		return;
	}

	//===================================
	// ESCキーで終了
	//===================================
	if (input_->GetKeyboard()->TriggerKey(DIK_ESCAPE)) {
		endRequest_ = true;
		return;
	}

	OutputDebugStringA(("currentEffectType_: " + std::to_string(postEffect_->GetCurrentEffectType()) + "\n").c_str());

	//===================================
	// ポストエフェクトのImGui表示
	//===================================
#ifdef _DEBUG

	postEffect_->ShowImGui();
#endif // _DEBUG

}

void Game::Draw() {
	// 1. RenderTextureにシーンを描画
	D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle =
		dxCore_->GetDsvHeap()->GetCPUDescriptorHandleForHeapStart();

	renderTexture_->BeginRender(dxCore_->GetCommandList(), &dsvHandle);

	srvManager_->PreDraw();

	// 深度クリア
	dxCore_->GetCommandList()->ClearDepthStencilView(
		dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

	// シーン描画
	SceneManager::GetInstance()->Draw();

	renderTexture_->EndRender(dxCore_->GetCommandList());

	// 2. Swapchainに切り替え
	dxCore_->BeginDraw();

	srvManager_->PreDraw();  // ディスクリプタヒープを再設定

	// 3. PostEffectでRenderTexture → Swapchainにコピー
	postEffect_->Draw(dxCore_->GetCommandList(), renderTexture_.get());

	// 4. ImGui描画（Swapchainに直接）
#ifdef _DEBUG

	CameraCapture::GetInstance()->LogDevicesToImGui();
	QRCodeReader::GetInstance()->OnImGui();
	TransitionManager::GetInstance()->OnImGui();
	ImGuiManager::Instance().EndFrame();
#endif // _DEBUG

	// 5. 終了
	dxCore_->EndDraw();
	spriteManager_->ClearIntermediateResources();
}

void Game::Finalize() {
	//===================================
	// PostEffectの終了処理
	//===================================
	if (postEffect_) {
		postEffect_->Finalize();
	}
	if (renderTexture_) {
		renderTexture_->Finalize();
	}

	//===================================
	// 基底クラスの終了処理
	// （SceneManagerとSceneFactoryの解放はFramework::Finalizeで行う）
	//===================================
	Framework::Finalize();
}