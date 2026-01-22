#include <cstdint>
#include <math.h>
#define _USE_MATH_DEFINES
#include <fstream>
#include <sstream>

// ComPtr
#include <wrl.h>

// DirectX12
#include <d3d12.h>
#include <dxgi1_6.h>
#include <cassert>
#include <dxgidebug.h>
#include <iostream>
#include <dxcapi.h>
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
#include "Vector3.h"
#include "Vector4.h"
#include "Transform.h"
#include "VertexData.h"
#include "Matrix4x4.h"
#include "Material.h"
#include "TransformationMatrix.h"
#include "DirectionalLight.h"
#include "ResourceObject.h"
#include "DebugCamera.h"
#include "WindowsApplication.h"
#include "DirectXCore.h"
#include "SpriteManager.h"
#include "SpriteInstance.h"
#include "Object3DManager.h"
#include "Object3DInstance.h"
#include "Log.h"
#include "ConvertString.h"
#include "MathUtility.h"
#include "TextureManager.h"
#include"ModelManager.h"
#include"Camera.h"
#include"SRVManager.h"
#include"ParticleManager.h"
#include"SoundManager.h"
#include "ControllerInput.h"
#include "MouseInput.h"
#include "InputManager.h"
#include "ImGuiManager.h"
#include "Debug.h"
#include "LightManager.h"

// 今のところ不良品
#include "ResourceManager.h"
#include "PathManager.h"

// 入力デバイス
#include "KeyboardInput.h"

//ImGUI
#include "imgui.h"
#include "imgui_impl_dx12.h"
#include "imgui_impl_win32.h"

// リリースチェック
struct D3DResourceLeakCheker {
	~D3DResourceLeakCheker() {
		// リソースリークチェック
		Microsoft::WRL::ComPtr<IDXGIDebug1> debug;
		if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&debug)))) {
			debug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_ALL);
			debug->ReportLiveObjects(DXGI_DEBUG_APP, DXGI_DEBUG_RLO_ALL);
			debug->ReportLiveObjects(DXGI_DEBUG_D3D12, DXGI_DEBUG_RLO_ALL);
		}
	}
};

// Windowsアプリでのエントリーポイント(main関数)
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
	D3DResourceLeakCheker leakCheck;

	// COMの初期化
	//CoInitializeEx(0, COINIT_MULTITHREADED);

	//--------ここ資料と違うけど警告出るから変更してる------------------------------------
	HRESULT hr = CoInitializeEx(0, COINIT_MULTITHREADED);
	assert(SUCCEEDED(hr));

	// クラスの生成
	ConvertStringClass* convertStringClass = new ConvertStringClass;
	DebugCamera* debugCamera = new DebugCamera;

	// Windowsアプリケーション生成
	WindowsApplication* winApp = new WindowsApplication();

	//========================
	// ウィンドウの初期化
	//========================
	// 初期化（タイトル）
	winApp->Initialize(L"GE3");

	DirectXCore* dxCore = new DirectXCore();
	dxCore->Initialize(winApp);

	// SRVManagerの初期化
	SRVManager* srvManager = new SRVManager();
	srvManager->Initialize(dxCore);

	// ImGuiManagerの初期化
	ImGuiManager::Instance().Initialize(
		winApp->GetHwnd(),
		dxCore,
		srvManager
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
	if (SUCCEEDED(dxCore->GetDevice()->QueryInterface(IID_PPV_ARGS(&infoQueue)))) {
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
			// Windows11でのDXGIデバッグレイヤーとDX12デバッグレイヤーの相互作用バグによるエラーメッセージ
			// https://stackoverFlow.com/question/69805245/directx-12-application-is-crashing-in-windows-11
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
	IDxcUtils* dxcUtils = nullptr;
	IDxcCompiler3* dxcCompiler = nullptr;
	hr = DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&dxcUtils));
	assert(SUCCEEDED(hr));
	hr = DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&dxcCompiler));
	assert(SUCCEEDED(hr));

	// 現時点でincludeはしないがincludeに対応するための設定を行う
	IDxcIncludeHandler* includeHandler = nullptr;
	hr = dxcUtils->CreateDefaultIncludeHandler(&includeHandler);
	assert(SUCCEEDED(hr));
	
	// ライトの初期化
	LightManager::GetInstance()->Initialize(dxCore);

	// Spriteの共通部分の初期化
	SpriteManager* spriteManager = new SpriteManager();
	spriteManager->Initialize(dxCore,srvManager);

	// テクスチャマネージャの初期化
	TextureManager::GetInstance()->Initialize(spriteManager,dxCore,srvManager);

	SpriteInstance* sprite = new SpriteInstance();
	sprite->Initialize(spriteManager, "Resources/uvChecker.png");

	// モデルマネージャーの初期化
	ModelManager::GetInstance()->Initialize(dxCore);

	// 3DObjectの共通部分の初期化
	Object3DManager* object3DManager = new Object3DManager();
	object3DManager->Initialize(dxCore);

	// カメラの生成
	Camera* camera = new Camera();
	camera->SetRotate({ 0.5f,0.0f,0.0f });
	camera->SetTranslate({ 0.0f,2.0f,-30.0f });
	object3DManager->SetDefaultCamera(camera);

	ParticleManager::GetInstance()->Initialize(dxCore, srvManager);
	ParticleManager::GetInstance()->SetCamera(camera);
	ParticleManager::GetInstance()->CreateParticleGroup("uvChecker", "Resources/uvChecker.png");
	ParticleManager::GetInstance()->CreateParticleGroup("circle", "Resources/circle.png");

	// 加速度フィールドの設定
	AccelerationField field;
	field.acceleration = { 15.0f, 0.0f, 0.0f };  // +x方向に15m/s²（風）
	field.area.min = { -1.0f, -1.0f, -1.0f }; // 範囲の最小点
	field.area.max = { 10.0f, 10.0f, 10.0f };    // 範囲の最大点
	ParticleManager::GetInstance()->SetAccelerationField(field);
	ParticleManager::GetInstance()->SetAccelerationFieldEnabled(true); // 有効化

	// SpriteInstance を複数保持する
	std::vector<SpriteInstance*> sprites;

	// 交互に使うスプライト
	const std::string textures[2] = {
		"Resources/uvChecker.png",
		"Resources/monsterBall.png"
	};

	// 5枚生成
	for (uint32_t i = 0; i < 5; ++i) {
		SpriteInstance* sprite = new SpriteInstance();
		const std::string& texturePath = textures[i % 2];

		// 名前を指定して初期化
		std::string spriteName = "Sprite_" + std::to_string(i);
		sprite->Initialize(spriteManager, texturePath, spriteName);

		sprite->SetSize({ 100.0f, 100.0f });
		sprite->SetPosition({ i * 2.0f, 0.0f });
		sprites.push_back(sprite);
	}

	// 3Dオブジェクトを配列で管理
	std::vector<Object3DInstance*> object3DInstances;
	const std::string modelFiles[] = { "monsterBall.obj", "terrain.obj","plane.gltf"};
	const std::string objectNames[] = { "MonsterBall", "terrain" ,"plane"};

	// 複数だすとき
	for (int i = 0; i < 3; ++i) {
		Object3DInstance* obj = new Object3DInstance();
		obj->Initialize(
			object3DManager,
			dxCore,
			"Resources",
			modelFiles[i],
			objectNames[i]  // 名前を指定
		);
		obj->SetTranslate({ 0.0f, 0.0f, 0.0f });
		object3DInstances.push_back(obj);
	}

	SoundManager::GetInstance()->Initialize();
	SoundManager::GetInstance()->LoadFile("fanfare", "Resources/fanfare.wav");

	//==============================
	// Inputの初期化
	//==============================
	InputManager* input = new InputManager();
	input->Initialize(winApp);
	
	//=========================
	// ViewportとScissor
	//=========================
	// ビューポート
	D3D12_VIEWPORT viewport{};

	// クライアント領域のサイズと一緒にして画面全体に表示
	viewport.Width = WindowsApplication::kClientWidth;
	viewport.Height = WindowsApplication::kClientHeight;
	viewport.TopLeftX = 0;
	viewport.TopLeftY = 0;
	viewport.MinDepth = 0.0f;
	viewport.MaxDepth = 1.0f;

	// シザー矩形
	D3D12_RECT scissorRect{};

	// 基本的にビューポートと同じ矩形が構成されるようにする
	scissorRect.left = 0;
	scissorRect.right = WindowsApplication::kClientWidth;
	scissorRect.top = 0;
	scissorRect.bottom = WindowsApplication::kClientHeight;

	//MSG msg{};
	// ウィンドウのxボタンが押されるまでループ
	while (winApp->ProcessMessage()) {


		// ===== ImGuiフレーム開始 =====
		ImGuiManager::Instance().BeginFrame();

		///
		// ここからゲームの処理
		///

		//=================================
		// ここから更新処理
		//=================================
		// 入力の更新
		input->Update();

		// --- キーボード入力 ---
		if (input->GetKeyboard()->TriggerKey(DIK_ESCAPE)) {
			PostQuitMessage(0);
		}

		if (input->GetKeyboard()->TriggerKey(DIK_SPACE)) {
			// スペースキーを押した瞬間
		}

		if (input->GetKeyboard()->PuhsKey(DIK_W)) {
			// Wキーを押している間
		}

		// マウス入力
		MouseInput* mouse = input->GetMouse();  // 変数に取っておくと楽

		if (mouse->IsButtonTriggered(MouseInput::Button::Left)) {
			// 左クリックした瞬間
		}

		// マウスの移動量
		LONG deltaX = mouse->GetDeltaX();
		LONG deltaY = mouse->GetDeltaY();

		// マウス座標
		LONG mouseX = mouse->GetClientX();
		LONG mouseY = mouse->GetClientY();

		// --- コントローラー入力 ---
		ControllerInput* controller = input->GetController();

		if (controller->IsConnected()) {
			// Aボタントリガー
			if (controller->IsButtonTriggered(XINPUT_GAMEPAD_A)) {
				// Aボタンを押した瞬間
			}

			// 左スティック
			const auto& leftStick = controller->GetLeftStick();
			float moveX = leftStick.x * leftStick.magnitude;
			float moveY = leftStick.y * leftStick.magnitude;

			// 振動
			if (controller->IsButtonTriggered(XINPUT_GAMEPAD_Y)) {
				controller->SetVibration(32000, 32000);
			}
		}

		//=========================
		// キーボードの入力処理
		//=========================
		// 数字の0のキーが押されていたら
		if (input->GetKeyboard()->TriggerKey(DIK_0)) {
			Debug::Log("trigger [0]");
		}

		if (input->GetKeyboard()->TriggerKey(DIK_1)) {
			SoundManager::GetInstance()->Play("fanfare");
			Debug::Log("trigger [1]");
		}

		if (input->GetKeyboard()->TriggerKey(DIK_2)) {
			SoundManager::GetInstance()->Stop("fanfare");
			Debug::Log("trigger [2]");
		}

		if (input->GetKeyboard()->TriggerKey(DIK_P)) {
			dxCore->SetUseFixedFrameRate(false);
		}

		// Fキーで場のON/OFF切り替え
		if (input->GetKeyboard()->TriggerKey(DIK_F)) {
			bool current = ParticleManager::GetInstance()->IsAccelerationFieldEnabled();
			ParticleManager::GetInstance()->SetAccelerationFieldEnabled(!current);
		}

		sprite->SetAnchorPoint({ 0.5f,0.5f });
		sprite->SetIsFlipX(true);

		// 回転テスト
		float rotation = sprite->GetRotation();
		rotation += 0.01f;
		sprite->SetRotation(rotation);

		// カメラの更新は必ずオブジェクトの更新前にやる
		camera->Update();

		for (Object3DInstance* obj : object3DInstances) {
			obj->Update();
		}

		// 更新処理
		ParticleManager::GetInstance()->Update();

		// 1秒間に発生させる量を自動制御
		static float emitTimer = 0.0f;
		emitTimer += dxCore->GetDeltaTime();
		float emitRate = ParticleManager::GetInstance()->GetEmitterSettings().emitRate;
		if (emitRate > 0.0f) {
			float emitInterval = 1.0f / emitRate;
			while (emitTimer >= emitInterval) {
				ParticleManager::GetInstance()->Emit("circle", { 0.0f, 0.0f, 0.0f }, 1);
				emitTimer -= emitInterval;
			}
		}

		// circleのパーティクルを常に発生させる
		ParticleManager::GetInstance()->Emit("circle", { 0.0f, 0.0f, 0.0f }, 1);

		// キー入力でパーティクル発生
		if (input->GetKeyboard()->TriggerKey(DIK_SPACE)) {
			ParticleManager::GetInstance()->Emit("uvChecker", { 0.0f, 0.0f, 0.0f }, 10);
		}

		sprite->Update();

		for (SpriteInstance* sprite : sprites) {
			sprite->Update();
		}

		debugCamera->Update(input->GetKeyboard()->keys_);

		// ESCAPEキーのトリガー入力で終了
		if (input->GetKeyboard()->TriggerKey(DIK_ESCAPE)) {
			PostQuitMessage(0);
		}

		///
		// ここまでゲームの処理
		///

		dxCore->BeginDraw();

		srvManager->PreDraw();

		//=========================
		// コマンドを積む
		//=========================
		dxCore->GetCommandList()->RSSetViewports(1, &viewport);                // Viewportを設定
		dxCore->GetCommandList()->RSSetScissorRects(1, &scissorRect);          // Scirssorを設定

		// 形状を設定。PSOに設定しているものとは別。同じものを設定。
		dxCore->GetCommandList()->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		// 指定した深度で画面全体をクリアする
		dxCore->GetCommandList()->ClearDepthStencilView(dxCore->GetDsvHeap()->GetCPUDescriptorHandleForHeapStart(), D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

		// 3Dオブジェクトの共通描画設定
		object3DManager->DrawSetting();
		LightManager::GetInstance()->BindLights(dxCore->GetCommandList());
		LightManager::GetInstance()->OnImGui();

		// 描画
		for (Object3DInstance* obj : object3DInstances) {
			obj->Draw(dxCore);
		}

		// 描画処理
		ParticleManager::GetInstance()->Draw();

		spriteManager->DrawSetting();
		sprite->Draw();

		for (SpriteInstance* sprite : sprites) {
			sprite->Draw();
		}

		// パーティクルのImGui表示
		ParticleManager::GetInstance()->OnImGui();

		// ImGui描画
		ImGuiManager::Instance().EndFrame();

		assert(dxCore->GetCommandList() != nullptr);
		dxCore->EndDraw();
		spriteManager->ClearIntermediateResources();
	}

	// 出力ウィンドウへの文字出力
	//OutputDebugStringA("Hello,DirectX!\n");
	Log("DirectX12isNotUseful\n");

	delete convertStringClass;

	// COMの終了
	CoUninitialize();

	//=============
	// 解放処理
	//=============
	// 生成と逆順に行う

	// 入力を解放
	input->Finalize();
	delete input;

	// 音声データ解放
	// 絶対にXAudio2を解放してから行うこと
	SoundManager::GetInstance()->Finalize();

	// パーティクル終了処理
	ParticleManager::GetInstance()->Finalize();

	// スプライト解放
	for (SpriteInstance* sprite : sprites) {
		delete sprite;
	}
	sprites.clear();
	delete sprite;

	// DirectXCoreよりも先に解放
	TextureManager::GetInstance()->Finalize();
	delete spriteManager;

	// 終了処理
	for (Object3DInstance* obj : object3DInstances) {
		delete obj;
	}
	object3DInstances.clear();

	delete object3DManager;
	ModelManager::GetInstance()->Finalize();

	LightManager::GetInstance()->Finalize();

	// ImGui終了処理
	ImGuiManager::Instance().Shutdown();

	// dxc関連
	includeHandler->Release();
	dxcCompiler->Release();
	dxcUtils->Release();
	delete srvManager;
	dxCore->Finalize();
	delete dxCore;

	// Debug
#ifdef _DEBUG

#endif

	CloseWindow(winApp->GetHwnd());

#ifdef _DEBUG

#endif // _DEBUG
	CloseWindow(winApp->GetHwnd());

	delete debugCamera;

	//======================
	// ReportLiveObjects
	//======================
	// 警告時に止まる
	//infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, true);

	winApp->Finalize();
	delete winApp;

	return 0;
}