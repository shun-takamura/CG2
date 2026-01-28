#include "Framework.h"

#include "DirectXTex.h"
#include "d3dx12.h"

#pragma comment(lib,"d3d12.lib")
#pragma comment(lib,"dxgi.lib")
#pragma comment(lib,"dxguid.lib")
#pragma comment(lib,"dxcompiler.lib")

//============================
// 自作ヘッダーのインクルード
//============================
#include "ConvertStringClass.h"
#include "WindowsApplication.h"
#include "DirectXCore.h"
#include "SpriteManager.h"
#include "Object3DManager.h"
#include "Log.h"
#include "TextureManager.h"
#include "ModelManager.h"
#include "SRVManager.h"
#include "ParticleManager.h"
#include "SoundManager.h"
#include "InputManager.h"
#include "ImGuiManager.h"
#include "LightManager.h"
#include "SceneManager.h"
#include "CameraCapture.h"

void Framework::Run() {
	// ゲームの初期化
	Initialize();

	while (true) { // ゲームループ
		// 毎フレーム更新
		Update();

		// 終了リクエストが来たら抜ける
		if (IsEndRequest()) {
			break;
		}

		// 描画
		Draw();
	}

	// ゲームの終了
	Finalize();
}

void Framework::Initialize() {
	// COMの初期化
	HRESULT hr = CoInitializeEx(0, COINIT_MULTITHREADED);
	assert(SUCCEEDED(hr));

	// クラスの生成
	convertStringClass_ = std::make_unique<ConvertStringClass>();

	// Windowsアプリケーション生成
	winApp_ = std::make_unique<WindowsApplication>();

	//========================
	// ウィンドウの初期化
	//========================
	// 初期化（タイトル）
	winApp_->Initialize(L"GE3");

	dxCore_ = std::make_unique<DirectXCore>();
	dxCore_->Initialize(winApp_.get());

	srvManager_ = std::make_unique<SRVManager>();
	srvManager_->Initialize(dxCore_.get());

	ImGuiManager::Instance().Initialize(
		winApp_->GetHwnd(),
		dxCore_.get(),
		srvManager_.get()
	);

	//========================================
	// ここにデバッグレイヤー デバックの時だけ出る
	//========================================
#ifdef _DEBUG
	Microsoft::WRL::ComPtr<ID3D12Debug1> debugController = nullptr;
	if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) {
		// デバッグレイヤーを有効化する
		debugController->EnableDebugLayer();

		// GPU側でもチェックを行うようにする
		debugController->SetEnableGPUBasedValidation(TRUE);
	}
#endif // DEBUG

	//=========================
	// エラー、警告が出たら即停止
	//=========================
#ifdef _DEBUG
	Microsoft::WRL::ComPtr<ID3D12InfoQueue> infoQueue = nullptr;
	if (SUCCEEDED(dxCore_->GetDevice()->QueryInterface(IID_PPV_ARGS(&infoQueue)))) {
		// ヤバいエラーの時に止まる
		infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, true);

		// エラー時に止まる
		infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, true);

		// 警告時に止まる
		infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, true);

		//======================
		// エラーと警告の抑制
		//======================
		// 抑制するメッセージのID
		D3D12_MESSAGE_ID denyIds[] = {
			D3D12_MESSAGE_ID_RESOURCE_BARRIER_MISMATCHING_COMMAND_LIST_TYPE
		};

		// 抑制するレベル
		D3D12_MESSAGE_SEVERITY severities[] = { D3D12_MESSAGE_SEVERITY_INFO };
		D3D12_INFO_QUEUE_FILTER filter{};
		filter.DenyList.NumIDs = _countof(denyIds);
		filter.DenyList.pIDList = denyIds;
		filter.DenyList.NumSeverities = _countof(severities);
		filter.DenyList.pSeverityList = severities;
		// 指定したメッセージの表示を抑制する
		infoQueue->PushStorageFilter(&filter);
	}
#endif // _DEBUG

	// dxcCompilerを初期化
	hr = DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&dxcUtils_));
	assert(SUCCEEDED(hr));
	hr = DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&dxcCompiler_));
	assert(SUCCEEDED(hr));

	// 現時点でincludeはしないがincludeに対応するための設定を行う
	hr = dxcUtils_->CreateDefaultIncludeHandler(&includeHandler_);
	assert(SUCCEEDED(hr));

	// ライトの初期化
	LightManager::GetInstance()->Initialize(dxCore_.get());

	// Spriteの共通部分の初期化
	spriteManager_ = std::make_unique<SpriteManager>();
	spriteManager_->Initialize(dxCore_.get(), srvManager_.get());

	// テクスチャマネージャの初期化
	TextureManager::GetInstance()->Initialize(spriteManager_.get(), dxCore_.get(), srvManager_.get());

	// モデルマネージャーの初期化
	ModelManager::GetInstance()->Initialize(dxCore_.get());

	// 3DObjectの共通部分の初期化
	object3DManager_ = std::make_unique<Object3DManager>();
	object3DManager_->Initialize(dxCore_.get());

	// パーティクルマネージャーの初期化
	ParticleManager::GetInstance()->Initialize(dxCore_.get(), srvManager_.get());

	// サウンドマネージャーの初期化
	SoundManager::GetInstance()->Initialize();

	// 初期化時（SoundManagerのInitialize後に呼ぶ）
	CameraCapture::GetInstance()->Initialize();

	//==============================
	// Inputの初期化
	//==============================
	input_ = std::make_unique<InputManager>();
	input_->Initialize(winApp_.get());

	//==============================
	// SceneManagerの初期化
	//==============================
	SceneManager::GetInstance()->Initialize(
		spriteManager_.get(),
		object3DManager_.get(),
		dxCore_.get(),
		srvManager_.get(),
		input_.get()
	);

	//=========================
	// ViewportとScissor
	//=========================
	// ビューポート
	viewport_.Width = WindowsApplication::kClientWidth;
	viewport_.Height = WindowsApplication::kClientHeight;
	viewport_.TopLeftX = 0;
	viewport_.TopLeftY = 0;
	viewport_.MinDepth = 0.0f;
	viewport_.MaxDepth = 1.0f;

	// シザー矩形
	scissorRect_.left = 0;
	scissorRect_.right = WindowsApplication::kClientWidth;
	scissorRect_.top = 0;
	scissorRect_.bottom = WindowsApplication::kClientHeight;
}

void Framework::Update() {
	// ウィンドウメッセージ処理
	if (!winApp_->ProcessMessage()) {
		endRequest_ = true;
		return;
	}

	// ===== ImGuiフレーム開始 =====
	ImGuiManager::Instance().BeginFrame();

	// 入力の更新
	input_->Update();

	// シーンマネージャーの更新
	SceneManager::GetInstance()->Update();
}

void Framework::Finalize() {
	// 出力ウィンドウへの文字出力
	Log("DirectX12isNotUseful\n");

	// COMの終了
	CoUninitialize();

	//=============
	// 解放処理
	//=============
	// 生成と逆順に行う

	// シーンマネージャーの終了処理
	SceneManager::GetInstance()->Finalize();

	// 入力を解放
	input_->Finalize();

	// 終了時（SoundManagerのFinalize前に呼ぶ）
	CameraCapture::GetInstance()->Finalize();

	// 音声データ解放
	SoundManager::GetInstance()->Finalize();

	// パーティクル終了処理
	ParticleManager::GetInstance()->Finalize();

	// DirectXCoreよりも先に解放
	TextureManager::GetInstance()->Finalize();
	LightManager::GetInstance()->Finalize();

	// ImGui終了処理
	ImGuiManager::Instance().Shutdown();

	dxCore_->Finalize();

	CloseWindow(winApp_->GetHwnd());
	winApp_->Finalize();

}