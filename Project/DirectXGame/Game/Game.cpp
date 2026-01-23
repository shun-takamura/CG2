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

Game::Game() {
}

Game::~Game() {
}

void Game::Initialize() {
	// 基底クラスの初期化処理
	Framework::Initialize();

	//===================================
	// SceneManagerの初期化
	//===================================
	SceneManager::GetInstance()->Initialize(
		spriteManager_,
		object3DManager_,
		dxCore_,
		srvManager_,
		input_
	);

	// 最初のシーンをタイトルシーンに設定
	SceneManager::GetInstance()->ChangeScene(SceneManager::TITLE);
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

	//===================================
	// シーンの更新
	//===================================
	SceneManager::GetInstance()->Update();
}

void Game::Draw() {
	dxCore_->BeginDraw();

	srvManager_->PreDraw();

	//=========================
	// コマンドを積む
	//=========================
	dxCore_->GetCommandList()->RSSetViewports(1, &viewport_);
	dxCore_->GetCommandList()->RSSetScissorRects(1, &scissorRect_);

	dxCore_->GetCommandList()->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	dxCore_->GetCommandList()->ClearDepthStencilView(
		dxCore_->GetDsvHeap()->GetCPUDescriptorHandleForHeapStart(),
		D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

	//===================================
	// シーンの描画
	//===================================
	SceneManager::GetInstance()->Draw();

	// ImGui描画
	ImGuiManager::Instance().EndFrame();

	assert(dxCore_->GetCommandList() != nullptr);
	dxCore_->EndDraw();
	spriteManager_->ClearIntermediateResources();
}

void Game::Finalize() {
	//===================================
	// SceneManagerの終了処理
	//===================================
	SceneManager::GetInstance()->Finalize();

	//===================================
	// 基底クラスの終了処理
	//===================================
	Framework::Finalize();
}