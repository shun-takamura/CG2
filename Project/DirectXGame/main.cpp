#include <cstdint>
//#include <format>
//#include <string>
#include <math.h>
#define _USE_MATH_DEFINES
//#include <vector>
#include <fstream>
#include <sstream>

// ComPtr
#include <wrl.h>

// Sounds
//#include <xaudio2.h>
//#pragma comment(lib,"xaudio2.lib")
// もう入ってるけどfstreamも必要だからね

// ============
// 入力デバイス
//=============
#define DIRECTINPUT_VERSION 0x0800 // Directinputのバージョン指定
#include <dinput.h> // 必ずバージョン指定後にインクルード
#pragma comment(lib,"dinput8.lib")
#pragma comment(lib,"dxguid.lib")

#include <XInput.h> // xboxコントローラー使うから
#pragma comment(lib,"xinput.lib")

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

//ImGUI
//#include "imgui.h"
//#include "imgui_impl_dx12.h"
//#include "imgui_impl_win32.h"
//
//extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

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

// 今のところ不良品
#include "ResourceManager.h"
#include "PathManager.h"

// 入力デバイス
#include "KeyboardInput.h"

//========================
// 構造体の定義
//========================

// 音声データの構造体
// ファイル構造によって色々ある

// チャンクヘッダ
//struct ChunkHeader
//{
//	char id[4]; // チャンク毎のID
//	int32_t size; // チャンクサイズ
//};
//
//// RIFFヘッダチャンク
//struct RiffHeader
//{
//	ChunkHeader chunk; // RIFF
//	char type[4]; // WAVE
//};
//
//// FMTチャンク
//struct FormatChunk
//{
//	ChunkHeader chunk; // fmt
//	WAVEFORMATEX fmt; // 波形フォーマット
//};

//// 音声データ
//struct SoundData
//{
//	// 波形フォーマット
//	WAVEFORMATEX wfex;
//
//	// バッファの先頭アドレス
//	BYTE* pBuffer;
//
//	// バッファのサイズ
//	unsigned int bufferSize;
//};

// デッドゾーンの設定
struct DeadZone {
	SHORT leftThumb;
	SHORT rightThumb;

	BYTE leftTrigger;
	BYTE rightTrigger;
};

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

//============================
// 関数の宣言
//============================

//==========================
// サウンドの関数
//==========================

/// <summary>
/// 音声データ読み込み関数
/// </summary>
/// <param name="filename">ファイル名</param>
/// <returns>音声データ</returns>
//SoundData SoundLoadWave(const char* filename);
//
///// <summary>
///// 音声データ解放関数
///// </summary>
///// <param name="soundData">解放する音声データ</param>
//void SoundUnload(SoundData* soundData);
//
//void SoundPlayWave(IXAudio2* xAudio2, const SoundData& soundData);

//=======================
// xboxコントローラーの関数
//=======================
void VibrateController(int controllerNum, WORD leftPower, WORD rightPower);

// 振動停止
void StopVibration(DWORD controllerNum);

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
	camera->SetTranslate({ 0.0f,2.0f,-20.0f });
	object3DManager->SetDefaultCamera(camera);

	ParticleManager::GetInstance()->Initialize(dxCore, srvManager);
	ParticleManager::GetInstance()->SetCamera(camera);
	ParticleManager::GetInstance()->CreateParticleGroup("uvChecker", "Resources/uvChecker.png");
	ParticleManager::GetInstance()->CreateParticleGroup("monsterBall", "Resources/monsterBall.png");

	// SpriteInstance を複数保持する
	std::vector<SpriteInstance*> sprites;

	// 交互に使うテクスチャ
	const std::string textures[2] = {
		"Resources/uvChecker.png",
		"Resources/monsterBall.png"
	};

	// 5枚生成
	for (uint32_t i = 0; i < 5; ++i) {
		SpriteInstance* sprite = new SpriteInstance();

		// iが偶数なら0、奇数なら1
		const std::string& texturePath = textures[i % 2];

		sprite->Initialize(spriteManager, texturePath);

		sprite->SetSize({ 100.0f, 100.0f });
		sprite->SetPosition({ i * 2.0f, 0.0f });

		sprites.push_back(sprite);
	}

	// 3Dオブジェクトを配列で管理
	std::vector<Object3DInstance*> object3DInstances;

	// 複数のモデルを作成
	const std::string modelFiles[] = { "axis.obj", "monsterBall.obj", "plane.obj" };
	for (int i = 0; i < 3; ++i) {
		Object3DInstance* obj = new Object3DInstance();
		obj->Initialize(object3DManager, dxCore, "Resources", modelFiles[i]);

		// 位置を設定（横に並べる）
		obj->SetTranslate({ i * 3.0f, 0.0f, 0.0f });

		object3DInstances.push_back(obj);
	}

	// soundsの変数の宣言
	Microsoft::WRL::ComPtr<IXAudio2> xAudio2;

	// IXAudio2MasteringVoiceはReleaseが無いからComPtr使えない
	// masterVoiceはxAudio2と一緒に解放される
	IXAudio2MasteringVoice* masterVoice;

	// 音声読み込み
	//SoundData soundData = SoundLoadWave("Resources/fanfare.wav");

	SoundManager::GetInstance()->Initialize();
	SoundManager::GetInstance()->LoadFile("fanfare", "Resources/fanfare.wav");

	//==============================
	// DirectInputの初期化
	//==============================
	// ここはデバイスを増やしても1つでいい
	IDirectInput8* directInput = nullptr;
	hr = DirectInput8Create(
		winApp->GetInstanceHandle(), DIRECTINPUT_VERSION, IID_IDirectInput8,
		(void**)&directInput, nullptr);
	assert(SUCCEEDED(hr));

	// キーボードインプットのポインタ
	KeyboardInput* keyboardInput = nullptr;

	// 入力の初期化
	keyboardInput = new KeyboardInput();
	keyboardInput->Initialize(winApp);

	// マウスデバイスの生成
	IDirectInputDevice8* mouse = nullptr;
	hr = directInput->CreateDevice(GUID_SysMouse, &mouse, NULL);// GUID_JoystickやGUID_Mouseとかでほかのデバイスも使える
	assert(SUCCEEDED(hr));

	// マウスの入力データ形式のセット
	hr = mouse->SetDataFormat(&c_dfDIMouse); // 標準形式。入力デバイスによっては複数用意されていたりする
	assert(SUCCEEDED(hr));

	// マウスの排他制御レベルのセット
	hr = mouse->SetCooperativeLevel(
		// DISCL_NONEXCLUSIVE : デバイスをこのアプリだけで占有しない
		winApp->GetHwnd(), DISCL_NONEXCLUSIVE
	);

	// xboxコントローラーの初期化
	XINPUT_STATE xinputState;

	// xboxコントローラーの初期化
	SHORT lX = 0;  // 左スティック X
	SHORT lY = 0;  // 左スティック Y
	SHORT rX = 0;  // 右スティック X
	SHORT rY = 0;  // 右スティック Y

	// 左右トリガー (0～255の範囲のアナログ値)
	BYTE leftTrigger = 0;   // 左トリガー
	BYTE rightTrigger = 0;  // 右トリガー

	// デッドゾーンの初期化(初期値はXInput標準値にしてある)
	DeadZone deadZone = {
		7849, // 左スティック
		7849, // 右スティック
		30,   // 左トリガー
		30    // 左トリガー
	};

	// xboxコントローラーのデッドゾーンのセッター(クラス化したら使う)
	//void SetLeftThumbDeadZone(SHORT* val) { deadZone.leftThumb = val; }
	//void SetRightThumbDeadZone(SHORT* val) { deadZone.rightThumb = val; }
	//void SetLeftTriggerDeadZone(SHORT* val) { deadZone.leftTrigger = val; }
	//void SetRightTriggerDeadZone(SHORT* val) { deadZone.rightTrigger = val; }

	//==============================
	// XAudio2の初期化
	//==============================
	// XAudio2エンジンのインスタンスを生成
	hr = XAudio2Create(&xAudio2, 0, XAUDIO2_DEFAULT_PROCESSOR);
	assert(SUCCEEDED(hr)); // インスタンス生成できなかったら停止

	// マスターボイスを生成
	hr = xAudio2->CreateMasteringVoice(&masterVoice);
	assert(SUCCEEDED(hr)); // マスターボイス生成できなかったら停止

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

	// サウンドを再生
	// キー入力ができるようになったらキー入力で再生とかの方が望ましい
	//SoundPlayWave(xAudio2.Get(), soundData);

	//================
	// ImGUIの初期化
	//================
	/*IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGui::StyleColorsDark();
	ImGui_ImplWin32_Init(winApp->GetHwnd());
	ImGui_ImplDX12_Init(dxCore->GetDevice(),
		dxCore->GetBackBufferCount(),
		rtvDesc.Format,
		srvDescriptorHeap.Get(),
		srvDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
		srvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());*/

	//MSG msg{};
	// ウィンドウのxボタンが押されるまでループ
	while (/*msg.message != WM_QUIT*/winApp->ProcessMessage()) {

		// ImGUIにここから始まることを通知
		/*ImGui_ImplDX12_NewFrame();
		ImGui_ImplWin32_NewFrame();
		ImGui::NewFrame();*/

		///
		// ここからゲームの処理
		///

		//=================================
		// ここから更新処理
		//=================================

		//=========================
		// キーボードの入力処理
		//=========================
		// 更新
		keyboardInput->Update();

		// 数字の0のキーが押されていたら
		if (keyboardInput->TriggerKey(DIK_0)) {
			OutputDebugStringA("trigger [0]\n");
		}

		if (keyboardInput->TriggerKey(DIK_1)) {
			SoundManager::GetInstance()->Play("fanfare");
			OutputDebugStringA("trigger [1]\n");
		}

		if (keyboardInput->TriggerKey(DIK_2)) {
			SoundManager::GetInstance()->Stop("fanfare");
			OutputDebugStringA("trigger [2]\n");
		}

		sprite->SetAnchorPoint({ 0.5f,0.5f });
		sprite->SetIsFlipX(true);
		// 回転テスト
		float rotation = sprite->GetRotation();
		rotation += 0.01f;
		sprite->SetRotation(rotation);

		// カメラの更新は必ずオブジェクトの更新まえにやる
		camera->Update();

		for (Object3DInstance* obj : object3DInstances) {
			obj->Update();
		}

		// 更新処理
		ParticleManager::GetInstance()->Update();

		// キー入力でパーティクル発生
		if (keyboardInput->TriggerKey(DIK_SPACE)) {
			ParticleManager::GetInstance()->Emit("uvChecker", { 0.0f, 0.0f, 0.0f }, 10);
		}
		if (keyboardInput->TriggerKey(DIK_SPACE)) {
			ParticleManager::GetInstance()->Emit("monsterBall", { 2.0f, 0.0f, 0.0f }, 5);
		}

		sprite->Update();

		for (SpriteInstance* sprite : sprites) {
			sprite->Update();
		}

		//=========================
		// マウスの入力処理
		//=========================

		//=========================
		// xboxコントローラーの入力処理
		//=========================
		// xboxコントローラーの入力情報を初期化
		ZeroMemory(&xinputState, sizeof(xinputState));

		// xboxコントローラーの情報の取得開始
		DWORD conResult = XInputGetState(0, &xinputState); // 0~3までの4つのコントローラーに対応可

		// xboxコントローラー接続時
		if (conResult == ERROR_SUCCESS) {

			// ボタン
			if (xinputState.Gamepad.wButtons & XINPUT_GAMEPAD_A) { /* Aボタンを押したときの処理 */ }
			if (xinputState.Gamepad.wButtons & XINPUT_GAMEPAD_B) { /* Bボタンを押したときの処理 */ }
			if (xinputState.Gamepad.wButtons & XINPUT_GAMEPAD_X) { /* Xボタンを押したときの処理 */ }
			if (xinputState.Gamepad.wButtons & XINPUT_GAMEPAD_Y) { /* Yボタンを押したときの処理 */ }

			// 十字キー
			if (xinputState.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_UP) { /* ↑を押したときの処理 */ }
			if (xinputState.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_DOWN) { /* ↓を押したときの処理 */ }
			if (xinputState.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_LEFT) { /* ←を押したときの処理 */ }
			if (xinputState.Gamepad.wButtons & XINPUT_GAMEPAD_DPAD_RIGHT) { /* →を押したときの処理 */ }

			// 左右スティック (−32768 ～ +32767 の範囲のアナログ値)
			lX = xinputState.Gamepad.sThumbLX;  // 左スティック X
			lY = xinputState.Gamepad.sThumbLY;  // 左スティック Y
			rX = xinputState.Gamepad.sThumbRX;  // 右スティック X
			rY = xinputState.Gamepad.sThumbRY;  // 右スティック Y

			// 左右トリガー (0～255の範囲のアナログ値)
			leftTrigger = xinputState.Gamepad.bLeftTrigger;   // 左トリガー
			rightTrigger = xinputState.Gamepad.bRightTrigger;  // 右トリガー

			//=================================
			// 円形デッドゾーンで補正（左スティック）
			//=================================
			{
				// スティックの倒れ具合（ベクトルの長さ）を計算
				float magnitude = std::sqrt(float(lX * lX + lY * lY));

				// 正規化方向ベクトル（単位ベクトル）
				float normalizedLX = 0.0f;
				float normalizedLY = 0.0f;

				// デッドゾーンを除いた後の正規化された大きさ（0.0～1.0）
				float normalizedMagnitude = 0.0f;

				// デッドゾーン外にある場合
				if (magnitude > deadZone.leftThumb) {
					// 最大値でクリップ（±32767）
					if (magnitude > 32767.0f) magnitude = 32767.0f;

					// デッドゾーン分を差し引く
					magnitude -= deadZone.leftThumb;

					// 0.0～1.0 に正規化
					normalizedMagnitude = magnitude / (32767.0f - deadZone.leftThumb);

					// 方向ベクトルを計算（magnitudeは0除算しないよう注意）
					normalizedLX = lX / std::sqrt(float(lX * lX + lY * lY));
					normalizedLY = lY / std::sqrt(float(lX * lX + lY * lY));
				} else {
					// デッドゾーン内は 0 扱い
					magnitude = 0.0f;
					normalizedMagnitude = 0.0f;
					normalizedLX = 0.0f;
					normalizedLY = 0.0f;
				}

				// ゲーム用の入力値として使える
				// normalizedLX, normalizedLY : 方向ベクトル
				// normalizedMagnitude        : 倒し具合（0.0～1.0）
			}

			//=================================
			// 円形デッドゾーンで補正（右スティック）
			//=================================
			{
				float magnitude = std::sqrt(float(rX * rX + rY * rY));
				float normalizedRX = 0.0f;
				float normalizedRY = 0.0f;
				float normalizedMagnitude = 0.0f;

				if (magnitude > deadZone.rightThumb) {
					if (magnitude > 32767.0f) magnitude = 32767.0f;
					magnitude -= deadZone.rightThumb;
					normalizedMagnitude = magnitude / (32767.0f - deadZone.rightThumb);

					normalizedRX = rX / std::sqrt(float(rX * rX + rY * rY));
					normalizedRY = rY / std::sqrt(float(rX * rX + rY * rY));
				} else {
					magnitude = 0.0f;
					normalizedMagnitude = 0.0f;
					normalizedRX = 0.0f;
					normalizedRY = 0.0f;
				}

				// ゲーム用の入力値として使える
				// normalizedRX, normalizedRY : 方向ベクトル
				// normalizedMagnitude        : 倒し具合（0.0～1.0）
			}

			//=========================
			// トリガーのデッドゾーン補正
			//=========================
			if (leftTrigger < deadZone.leftTrigger)  leftTrigger = 0;
			if (rightTrigger < deadZone.rightTrigger) rightTrigger = 0;

		}

		// 開発用UI処理。実際に開発用のuiを出す場合はここをゲーム固有の処理に置き換える.
		// ImGui フレーム開始直後
		//ImGui::Begin("Triangle Settings");

		//// ModelTransform
		//ImGui::Text("ModelTransform");
		//ImGui::DragFloat3("Scale", &transform.scale.x, 0.01f);
		//ImGui::DragFloat3("Rotate", &transform.rotate.x, 0.01f);
		//ImGui::DragFloat3("Translate", &transform.translate.x, 1.0f);
		//ImGui::ColorEdit4("ColorEdit", &materialData->color.x);

		// SpriteTransform（Sprite自体の見た目位置など）
		//ImGui::Checkbox("Show Sprite", &isSpriteVisible);
		//ImGui::Text("SpriteTransform");
		//ImGui::DragFloat3("Scale##Sprite", &transformSprite.scale.x, 0.01f);
		//ImGui::DragFloat3("Rotate##Sprite", &transformSprite.rotate.x, 0.01f);
		//ImGui::DragFloat3("Translate##Sprite", &transformSprite.translate.x, 1.0f);

		//// UVTransform（テクスチャの位置・回転など）
		//ImGui::Text("UVTransform");
		//ImGui::DragFloat2("Scale##UV", &uvTransformSprite.scale.x, 0.01f);
		//ImGui::SliderAngle("Rotate##UV", &uvTransformSprite.rotate.z);
		//ImGui::DragFloat2("Translate##UV", &uvTransformSprite.translate.x, 0.01f);

		//// カメラ操作
		//ImGui::Text("DebugCamera");
		//ImGui::Checkbox("Use Debug Camera", reinterpret_cast<bool*>(&isUseDebugCamera));

		//// Light
		//ImGui::Text("Light");

		//const char* lightingItems[] = { "None", "Lambert", "Half Lambert" };
		//int currentLightingIndex = static_cast<int>(lightingMode);
		//if (ImGui::Combo("Lighting", &currentLightingIndex, lightingItems, IM_ARRAYSIZE(lightingItems))) {
		//	lightingMode = static_cast<LightingMode>(currentLightingIndex);
		//}

		////ImGui::Checkbox("Use Half Lambert", reinterpret_cast<bool*>(&isUseHalfLambert));
		//ImGui::ColorEdit4("Color", &directionalLightData->color.x);
		//ImGui::DragFloat3("Direction", &directionalLightData->direction.x, 0.01f);
		//ImGui::DragFloat("Intensity", &directionalLightData->intensity, 0.01f);

		//ImGui::End();

		//===================================
		// デバッグカメラの操作説明(別ウィンドウ)
		//===================================
		/*ImGui::Begin("Debug Camera Controls");

		ImGui::Text("=== Movement ===");
		ImGui::BulletText("W : Zoom In");
		ImGui::BulletText("S : Zoom Out");
		ImGui::BulletText("A : Move Left");
		ImGui::BulletText("D : Move Right");
		ImGui::BulletText("Q : Move Down");
		ImGui::BulletText("E : Move Up");

		ImGui::Spacing();
		ImGui::Text("=== Rotation ===");
		ImGui::BulletText("Up Arrow    : Pitch Up");
		ImGui::BulletText("Down Arrow  : Pitch Down");
		ImGui::BulletText("Left Arrow  : Yaw Left");
		ImGui::BulletText("Right Arrow : Yaw Right");
		ImGui::BulletText("O : Roll -");
		ImGui::BulletText("P : Roll +");

		ImGui::End();*/

		debugCamera->Update(keyboardInput->keys_);

		// ESCAPEキーのトリガー入力で終了
		if (keyboardInput->TriggerKey(DIK_ESCAPE)) {
			PostQuitMessage(0);
		}

		///
		// ここまでゲームの処理
		///

		// ゲームの処理が終わったので描画処理前にImGUIの内部コマンドを生成
		//ImGui::Render();

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

		// 描画
		for (Object3DInstance* obj : object3DInstances) {
			obj->Draw(dxCore);
		}

		// 描画処理
		ParticleManager::GetInstance()->SetBlendMode(ParticleManager::kBlendModeAdd);
		ParticleManager::GetInstance()->Draw();

		spriteManager->DrawSetting();
		sprite->Draw();

		for (SpriteInstance* sprite : sprites) {
			sprite->Draw();
		}

		// 諸々の描画が終わってからImGUIの描画を行う(手前に出さなきゃいけないからねぇ)
		// 実際のCommandListのImGUIの描画コマンドを積む
		//ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), dxCore->GetCommandList());
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

	// ImGUIの終了処理
	/*ImGui_ImplDX12_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();*/

	// 入力を解放
	delete keyboardInput;

	// XAudio2解放
	//xAudio2.Reset();

	// 音声データ解放
	// 絶対にXAudio2を解放してから行うこと
	SoundManager::GetInstance()->Finalize();
	//SoundUnload(&soundData);

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

//============================
// 関数の定義
//============================
//
//SoundData SoundLoadWave(const char* filename)
//{
//	//HRESULT result;
//
//	//===========================
//	// ファイルを開く
//	//===========================
//	// fstreamのインスタンス
//	std::ifstream file;
//
//	// .wavファイルをバイナリモードで開く
//	file.open(filename, std::ios_base::binary);
//
//	// ファイルが開けなかったら停止
//	assert(file.is_open());
//
//	//===========================
//	// .wavデータを読み込み
//	//===========================
//	// RIFFヘッダの読み込み
//	RiffHeader riff;
//	file.read((char*)&riff, sizeof(riff));
//
//	// ファイルがRIFFかチェック
//	if (strncmp(riff.chunk.id, "RIFF", 4) != 0) {
//		assert(0);
//	}
//
//	// RIFFならファイルタイプがWAVEかチェック
//	// MP3を拡張子だけ.wavにしてもダメ。やりたいなら変換して
//	if (strncmp(riff.type, "WAVE", 4) != 0) {
//		assert(0);
//	}
//
//	// 一時的にチャンクヘッダ（IDとサイズ）を格納する変数を用意
//	ChunkHeader chunk = {};
//
//	// ファイルからチャンクヘッダ（4バイトのIDと4バイトのサイズ）を読み込む
//	file.read((char*)&chunk, sizeof(ChunkHeader));
//
//	// チャンクIDが "fmt " であることを確認（フォーマットチャンクでなければ停止）
//	if (strncmp(chunk.id, "fmt ", 4) != 0) {
//		assert(0);  // フォーマットチャンク以外だった場合は処理を中断
//	}
//
//	// フォーマットチャンク用の構造体を初期化
//	FormatChunk format = {};
//
//	// 読み込んだチャンクヘッダ情報を FormatChunk にコピー
//	format.chunk = chunk;
//
//	// ファイルからチャンク本体（波形フォーマット情報）を読み込む(40Byte対応Ver)
//	// サイズはチャンクヘッダに記録されていたサイズに基づいて読み込む
//	const size_t kFmtLimitSize = sizeof(format.fmt);             // 通常18バイト
//	std::vector<char> buffer(chunk.size);                        // チャンクサイズ分の一時バッファを確保
//	file.read(buffer.data(), chunk.size);                        // チャンクサイズ分を読み込む
//	std::memcpy(&format.fmt, buffer.data(), kFmtLimitSize);      // 先頭kFmtLimitSize分だけをコピー
//
//	// Dataチャンクの読み込み
//	ChunkHeader data;
//	file.read((char*)&data, sizeof(data));
//
//	// JUNKチャンクを検知した場合の処理
//	if (strncmp(data.id, "JUNK", 4) == 0) {
//		// 読み取り位置をJUNKチャンクの終わりまで進める
//		file.seekg(data.size, std::ios_base::cur);
//
//		// 再読み込み
//		file.read((char*)&data, sizeof(data));
//	}
//
//	if (strncmp(data.id, "data", 4) != 0) {
//		assert(0);
//	}
//
//	// Dataチャンクのデータ部(波形データ)の読み込み
//	// Dataチャンクは音声ファイルごとに異なるのでバッファを動的確保
//	char* pBuffer = new char[data.size];
//	file.read(pBuffer, data.size);
//
//	//===========================
//	// ファイルを閉じる
//	//===========================
//	// WAVEファイルを閉じる
//	file.close();
//
//	//===========================
//	// SoundDataを返す
//	//===========================
//	SoundData soundData = {};
//
//	soundData.wfex = format.fmt; // 波形フォーマット
//	soundData.pBuffer = reinterpret_cast<BYTE*>(pBuffer); // 波形データ
//	soundData.bufferSize = data.size; // 波形データのサイズ
//
//	return soundData;
//}
//
//void SoundUnload(SoundData* soundData)
//{
//	// バッファのメモリを解放
//	delete[] soundData->pBuffer;
//
//	soundData->pBuffer = 0;
//	soundData->bufferSize = 0;
//	soundData->wfex = {};
//}
//
//void SoundPlayWave(IXAudio2* xAudio2, const SoundData& soundData)
//{
//	HRESULT result;
//
//	// 波形フォーマットを基にSourceVoiceの生成
//	IXAudio2SourceVoice* pSourceVoice = nullptr;
//	result = xAudio2->CreateSourceVoice(&pSourceVoice, &soundData.wfex);
//	assert(SUCCEEDED(result));
//
//	// 再生する波形データの設定
//	XAUDIO2_BUFFER buf{};
//	buf.pAudioData = soundData.pBuffer;
//	buf.AudioBytes = soundData.bufferSize;
//	buf.Flags = XAUDIO2_END_OF_STREAM;
//
//	// 波形データの再生
//	result = pSourceVoice->SubmitSourceBuffer(&buf);
//	result = pSourceVoice->Start();
//}

void VibrateController(int controllerNum, WORD leftPower, WORD rightPower) {
	XINPUT_VIBRATION vibration;

	// ゼロにクリア
	ZeroMemory(&vibration, sizeof(vibration));

	// 振動の強さを設定
	vibration.wLeftMotorSpeed = leftPower; // 左モーター
	vibration.wRightMotorSpeed = rightPower; // 右モーター

	// コントローラーを指定して振動
	XInputSetState(controllerNum, &vibration);
}

void StopVibration(DWORD controllerNum) {
	VibrateController(controllerNum, 0, 0); // 指定したコントローラーのモーターを止める
}
