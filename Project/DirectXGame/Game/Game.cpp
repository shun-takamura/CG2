#include "Game.h"

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
#include "ModelManager.h"
#include "Camera.h"
#include "SRVManager.h"
#include "ParticleManager.h"
#include "SoundManager.h"
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

Game::Game() {
}

Game::~Game() {
}

void Game::Initialize() {
	// COMの初期化
	HRESULT hr = CoInitializeEx(0, COINIT_MULTITHREADED);
	assert(SUCCEEDED(hr));

	// クラスの生成
	convertStringClass_ = new ConvertStringClass;
	debugCamera_ = new DebugCamera;

	// Windowsアプリケーション生成
	winApp_ = new WindowsApplication();

	//========================
	// ウィンドウの初期化
	//========================
	// 初期化（タイトル）
	winApp_->Initialize(L"GE3");

	dxCore_ = new DirectXCore();
	dxCore_->Initialize(winApp_);

	// SRVManagerの初期化
	srvManager_ = new SRVManager();
	srvManager_->Initialize(dxCore_);

	// ImGuiManagerの初期化
	ImGuiManager::Instance().Initialize(
		winApp_->GetHwnd(),
		dxCore_,
		srvManager_
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
	hr = DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&dxcUtils_));
	assert(SUCCEEDED(hr));
	hr = DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&dxcCompiler_));
	assert(SUCCEEDED(hr));

	// 現時点でincludeはしないがincludeに対応するための設定を行う
	hr = dxcUtils_->CreateDefaultIncludeHandler(&includeHandler_);
	assert(SUCCEEDED(hr));

	// ライトの初期化
	LightManager::GetInstance()->Initialize(dxCore_);

	// Spriteの共通部分の初期化
	spriteManager_ = new SpriteManager();
	spriteManager_->Initialize(dxCore_, srvManager_);

	// テクスチャマネージャの初期化
	TextureManager::GetInstance()->Initialize(spriteManager_, dxCore_, srvManager_);

	sprite_ = new SpriteInstance();
	sprite_->Initialize(spriteManager_, "Resources/uvChecker.png");

	// モデルマネージャーの初期化
	ModelManager::GetInstance()->Initialize(dxCore_);

	// 3DObjectの共通部分の初期化
	object3DManager_ = new Object3DManager();
	object3DManager_->Initialize(dxCore_);

	// カメラの生成
	camera_ = new Camera();
	camera_->SetRotate({ 0.5f,0.0f,0.0f });
	camera_->SetTranslate({ 0.0f,2.0f,-30.0f });
	object3DManager_->SetDefaultCamera(camera_);

	ParticleManager::GetInstance()->Initialize(dxCore_, srvManager_);
	ParticleManager::GetInstance()->SetCamera(camera_);
	ParticleManager::GetInstance()->CreateParticleGroup("uvChecker", "Resources/uvChecker.png");
	ParticleManager::GetInstance()->CreateParticleGroup("circle", "Resources/circle.png");

	// 加速度フィールドの設定
	AccelerationField field;
	field.acceleration = { 15.0f, 0.0f, 0.0f };  // +x方向に15m/s²（風）
	field.area.min = { -1.0f, -1.0f, -1.0f }; // 範囲の最小点
	field.area.max = { 10.0f, 10.0f, 10.0f };    // 範囲の最大点
	ParticleManager::GetInstance()->SetAccelerationField(field);
	ParticleManager::GetInstance()->SetAccelerationFieldEnabled(true); // 有効化

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
		sprite->Initialize(spriteManager_, texturePath, spriteName);

		sprite->SetSize({ 100.0f, 100.0f });
		sprite->SetPosition({ i * 2.0f, 0.0f });
		sprites_.push_back(sprite);
	}

	// 3Dオブジェクトを配列で管理
	const std::string modelFiles[] = { "monsterBall.obj", "terrain.obj","plane.gltf" };
	const std::string objectNames[] = { "MonsterBall", "terrain" ,"plane" };

	// 複数だすとき
	for (int i = 0; i < 3; ++i) {
		Object3DInstance* obj = new Object3DInstance();
		obj->Initialize(
			object3DManager_,
			dxCore_,
			"Resources",
			modelFiles[i],
			objectNames[i]  // 名前を指定
		);
		obj->SetTranslate({ 0.0f, 0.0f, 0.0f });
		object3DInstances_.push_back(obj);
	}

	SoundManager::GetInstance()->Initialize();
	SoundManager::GetInstance()->LoadFile("fanfare", "Resources/fanfare.wav");

	//==============================
	// Inputの初期化
	//==============================
	input_ = new InputManager();
	input_->Initialize(winApp_);

	//=========================
	// ViewportとScissor
	//=========================
	// ビューポート
	// クライアント領域のサイズと一緒にして画面全体に表示
	viewport_.Width = WindowsApplication::kClientWidth;
	viewport_.Height = WindowsApplication::kClientHeight;
	viewport_.TopLeftX = 0;
	viewport_.TopLeftY = 0;
	viewport_.MinDepth = 0.0f;
	viewport_.MaxDepth = 1.0f;

	// シザー矩形
	// 基本的にビューポートと同じ矩形が構成されるようにする
	scissorRect_.left = 0;
	scissorRect_.right = WindowsApplication::kClientWidth;
	scissorRect_.top = 0;
	scissorRect_.bottom = WindowsApplication::kClientHeight;
}

void Game::Update() {
	// ウィンドウメッセージ処理
	if (!winApp_->ProcessMessage()) {
		isEndRequest_ = true;
		return;
	}

	// ===== ImGuiフレーム開始 =====
	ImGuiManager::Instance().BeginFrame();

	//=================================
	// ここから更新処理
	//=================================
	// 入力の更新
	input_->Update();

	// --- キーボード入力 ---
	if (input_->GetKeyboard()->TriggerKey(DIK_ESCAPE)) {
		isEndRequest_ = true;
		return;
	}

	if (input_->GetKeyboard()->TriggerKey(DIK_SPACE)) {
		// スペースキーを押した瞬間
	}

	if (input_->GetKeyboard()->PuhsKey(DIK_W)) {
		// Wキーを押している間
	}

	// マウス入力
	MouseInput* mouse = input_->GetMouse();  // 変数に取っておくと楽

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
	ControllerInput* controller = input_->GetController();

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
	if (input_->GetKeyboard()->TriggerKey(DIK_0)) {
		Debug::Log("trigger [0]");
	}

	if (input_->GetKeyboard()->TriggerKey(DIK_1)) {
		SoundManager::GetInstance()->Play("fanfare");
		Debug::Log("trigger [1]");
	}

	if (input_->GetKeyboard()->TriggerKey(DIK_2)) {
		SoundManager::GetInstance()->Stop("fanfare");
		Debug::Log("trigger [2]");
	}

	if (input_->GetKeyboard()->TriggerKey(DIK_P)) {
		dxCore_->SetUseFixedFrameRate(false);
	}

	// Fキーで場のON/OFF切り替え
	if (input_->GetKeyboard()->TriggerKey(DIK_F)) {
		bool current = ParticleManager::GetInstance()->IsAccelerationFieldEnabled();
		ParticleManager::GetInstance()->SetAccelerationFieldEnabled(!current);
	}

	sprite_->SetAnchorPoint({ 0.5f,0.5f });
	sprite_->SetIsFlipX(true);

	// 回転テスト
	float rotation = sprite_->GetRotation();
	rotation += 0.01f;
	sprite_->SetRotation(rotation);

	// カメラの更新は必ずオブジェクトの更新前にやる
	camera_->Update();

	for (Object3DInstance* obj : object3DInstances_) {
		obj->Update();
	}

	// 更新処理
	ParticleManager::GetInstance()->Update();

	// 1秒間に発生させる量を自動制御
	emitTimer_ += dxCore_->GetDeltaTime();
	float emitRate = ParticleManager::GetInstance()->GetEmitterSettings().emitRate;
	if (emitRate > 0.0f) {
		float emitInterval = 1.0f / emitRate;
		while (emitTimer_ >= emitInterval) {
			ParticleManager::GetInstance()->Emit("circle", { 0.0f, 0.0f, 0.0f }, 1);
			emitTimer_ -= emitInterval;
		}
	}

	// circleのパーティクルを常に発生させる
	ParticleManager::GetInstance()->Emit("circle", { 0.0f, 0.0f, 0.0f }, 1);

	// キー入力でパーティクル発生
	if (input_->GetKeyboard()->TriggerKey(DIK_SPACE)) {
		ParticleManager::GetInstance()->Emit("uvChecker", { 0.0f, 0.0f, 0.0f }, 10);
	}

	sprite_->Update();

	for (SpriteInstance* sprite : sprites_) {
		sprite->Update();
	}

	debugCamera_->Update(input_->GetKeyboard()->keys_);
}

void Game::Draw() {
	dxCore_->BeginDraw();

	srvManager_->PreDraw();

	//=========================
	// コマンドを積む
	//=========================
	dxCore_->GetCommandList()->RSSetViewports(1, &viewport_);                // Viewportを設定
	dxCore_->GetCommandList()->RSSetScissorRects(1, &scissorRect_);          // Scirssorを設定

	// 形状を設定。PSOに設定しているものとは別。同じものを設定。
	dxCore_->GetCommandList()->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	// 指定した深度で画面全体をクリアする
	dxCore_->GetCommandList()->ClearDepthStencilView(dxCore_->GetDsvHeap()->GetCPUDescriptorHandleForHeapStart(), D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

	// 3Dオブジェクトの共通描画設定
	object3DManager_->DrawSetting();
	LightManager::GetInstance()->BindLights(dxCore_->GetCommandList());
	LightManager::GetInstance()->OnImGui();

	// 描画
	for (Object3DInstance* obj : object3DInstances_) {
		obj->Draw(dxCore_);
	}

	// 描画処理
	ParticleManager::GetInstance()->Draw();

	spriteManager_->DrawSetting();
	sprite_->Draw();

	for (SpriteInstance* sprite : sprites_) {
		sprite->Draw();
	}

	// パーティクルのImGui表示
	ParticleManager::GetInstance()->OnImGui();

	// ImGui描画
	ImGuiManager::Instance().EndFrame();

	assert(dxCore_->GetCommandList() != nullptr);
	dxCore_->EndDraw();
	spriteManager_->ClearIntermediateResources();
}

void Game::Finalize() {
	// 出力ウィンドウへの文字出力
	Log("DirectX12isNotUseful\n");

	delete convertStringClass_;

	// COMの終了
	CoUninitialize();

	//=============
	// 解放処理
	//=============
	// 生成と逆順に行う

	// 入力を解放
	input_->Finalize();
	delete input_;

	// 音声データ解放
	// 絶対にXAudio2を解放してから行うこと
	SoundManager::GetInstance()->Finalize();

	// パーティクル終了処理
	ParticleManager::GetInstance()->Finalize();

	// スプライト解放
	for (SpriteInstance* sprite : sprites_) {
		delete sprite;
	}
	sprites_.clear();
	delete sprite_;

	// DirectXCoreよりも先に解放
	TextureManager::GetInstance()->Finalize();
	delete spriteManager_;

	// 終了処理
	for (Object3DInstance* obj : object3DInstances_) {
		delete obj;
	}
	object3DInstances_.clear();

	delete object3DManager_;
	ModelManager::GetInstance()->Finalize();

	LightManager::GetInstance()->Finalize();

	// ImGui終了処理
	ImGuiManager::Instance().Shutdown();

	// dxc関連
	includeHandler_->Release();
	dxcCompiler_->Release();
	dxcUtils_->Release();
	delete srvManager_;
	dxCore_->Finalize();
	delete dxCore_;

	CloseWindow(winApp_->GetHwnd());

	delete debugCamera_;

	winApp_->Finalize();
	delete winApp_;
}