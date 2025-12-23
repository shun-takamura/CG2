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
#include <xaudio2.h>
#pragma comment(lib,"xaudio2.lib")
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
#include "imgui.h"
#include "imgui_impl_dx12.h"
#include "imgui_impl_win32.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

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
#include "Log.h"
#include "ConvertString.h"
#include "MathUtility.h"

// 今のところ不良品
#include "ResourceManager.h"
#include "PathManager.h"

// 入力デバイス
#include "KeyboardInput.h"

//========================
// 構造体の定義
//========================
enum class LightingMode {
	None, // ライティング無し
	Lambert, // ランバート反射
	HalfLambert // 
};

// ブレンドモード
enum BlendMode {
	// ブレンド無し
	kBlendModeNone,

	// 通常αブレンド。デフォルト。Src*SrcA+Dest*(1-SrcA)
	kBlendModeNormal,

	// 加算 Src*SrcA+Dest*1
	kBlendModeAdd,

	// 減算 Dest*1-Src*SrcA
	kBlendModeSubtract,

	// 乗算 Src*0+Dest*Src
	kBlendModeMultily,

	// スクリーン Src*(1-Dest)+Dest*1
	kBlendModeScreen,

	// 利用してはいけない
	kCountOfBlendMode
};

// 音声データの構造体
// ファイル構造によって色々ある

// チャンクヘッダ
struct ChunkHeader
{
	char id[4]; // チャンク毎のID
	int32_t size; // チャンクサイズ
};

// RIFFヘッダチャンク
struct RiffHeader
{
	ChunkHeader chunk; // RIFF
	char type[4]; // WAVE
};

// FMTチャンク
struct FormatChunk
{
	ChunkHeader chunk; // fmt
	WAVEFORMATEX fmt; // 波形フォーマット
};

// 音声データ
struct SoundData
{
	// 波形フォーマット
	WAVEFORMATEX wfex;

	// バッファの先頭アドレス
	BYTE* pBuffer;

	// バッファのサイズ
	unsigned int bufferSize;
};

// モデルデータの構造体
struct MaterialData {
	std::string textureFilePath;
};

struct ModelData {
	std::vector<VertexData> vertices;
	MaterialData material;
};

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

IDxcBlob* CompileShader(const std::wstring& filePath, const wchar_t* profile, IDxcUtils* dxcUtils, IDxcCompiler3* dxcCompiler, IDxcIncludeHandler* includeHandler);

//Microsoft::WRL::ComPtr<ID3D12Resource> CreateBufferResource(Microsoft::WRL::ComPtr<ID3D12Device> device, size_t sizeInBytes);

Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>CreateDescriptorHeap(
	Microsoft::WRL::ComPtr<ID3D12Device> device, D3D12_DESCRIPTOR_HEAP_TYPE heapType, UINT numDescriptor, bool shaderVicible);

//============================
// テクスチャの関数
//============================

/// <summary>
/// Textureデータを読み込む関数
/// </summary>
/// <param name="filename">ファイルパス</param>
/// <returns>ミップマップ付きのデータ</returns>
DirectX::ScratchImage LoadTexture(const std::string& filePath);

/// <summary>
/// DirectX12のTetureResourceを作る関数
/// </summary>
/// <param name="device"></param>
/// <param name="metadata"></param>
/// <returns></returns>
Microsoft::WRL::ComPtr<ID3D12Resource> CreateTextureResource(Microsoft::WRL::ComPtr<ID3D12Device> device, const DirectX::TexMetadata& metadata);

/// <summary>
/// TextureResourceにデータを転送する関数
/// </summary>
/// <param name="texture"></param>
/// <param name="mipImages"></param>
void UploadTextureData(Microsoft::WRL::ComPtr<ID3D12Resource> texture, const DirectX::ScratchImage& mipImages);

[[nodiscard]]
//Microsoft::WRL::ComPtr<ID3D12Resource> UploadTextureData(Microsoft::WRL::ComPtr<ID3D12Resource> texture, const DirectX::ScratchImage& mipImages, Microsoft::WRL::ComPtr<ID3D12Device> device, ID3D12GraphicsCommandList* commandList);

Microsoft::WRL::ComPtr<ID3D12Resource> CreateDepthStencilTextureResource(Microsoft::WRL::ComPtr<ID3D12Device> device, int32_t width, int32_t height);

//==========================
// サウンドの関数
//==========================

/// <summary>
/// 音声データ読み込み関数
/// </summary>
/// <param name="filename">ファイル名</param>
/// <returns>音声データ</returns>
SoundData SoundLoadWave(const char* filename);

/// <summary>
/// 音声データ解放関数
/// </summary>
/// <param name="soundData">解放する音声データ</param>
void SoundUnload(SoundData* soundData);


void SoundPlayWave(IXAudio2* xAudio2, const SoundData& soundData);

//==============================
// モデルの関数
//==============================
ModelData LoadObjFile(const std::string& directorPath, const std::string& filename);

MaterialData LoadMaterialTemplateFile(const std::string& directoryPath, const std::string& filename);

//=======================
// xboxコントローラーの関数
//=======================
void VibrateController(int controllerNum, WORD leftPower, WORD rightPower);

// 振動停止
void StopVibration(DWORD controllerNum);

//void DrawSphere(const Vector3& center, float radius, const Matrix4x4& viewProjectionMatrix, const Matrix4x4& viewportMatrix, uint32_t color);

D3D12_CPU_DESCRIPTOR_HANDLE GetCPUDescriptorHandle(Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>descriptorHeap, uint32_t descriptorSize, uint32_t index);

D3D12_GPU_DESCRIPTOR_HANDLE GetGPUDescriptorHandle(Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> descriptorHeap, uint32_t descriptorSize, uint32_t index);

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
	spriteManager->Initialize(dxCore);

	SpriteInstance* sprite = new SpriteInstance();
	sprite->Initialize(spriteManager, "Resources/uvChecker.png");

	// SpriteInstance を複数保持する
	std::vector<SpriteInstance*> sprites;
	
	// 5枚生成
	for (uint32_t i = 0; i < 5; ++i) {
		SpriteInstance* sprite = new SpriteInstance();
		sprite->Initialize(spriteManager, "Resources/uvChecker.png");

		sprite->SetSize({ 100.0f,100.0f });
		sprite->SetPosition({0.0f+ i * 2.0f, 0.0f });

		sprites.push_back(sprite);
		Log("generateCount\n");
	}

	// soundsの変数の宣言
	Microsoft::WRL::ComPtr<IXAudio2> xAudio2;

	// IXAudio2MasteringVoiceはReleaseが無いからComPtr使えない
	// masterVoiceはxAudio2と一緒に解放される
	IXAudio2MasteringVoice* masterVoice;

	// 音声読み込み
	SoundData soundData = SoundLoadWave("Resources/fanfare.wav");

	// モデル読み込み
	ModelData modelData = LoadObjFile("Resources", "fence.obj");

	// 板ポリ=====================================================================================================================
	//ModelData modelData2;
	//modelData2.vertices.push_back({ .position = {  -1.0f,  1.0f, 0.0f, 1.0f }, .texcoord = { 0.0f, 0.0f }, .normal = { 0.0f, 0.0f, 1.0f } }); // 左上
	//modelData2.vertices.push_back({ .position = {  1.0f, 1.0f, 0.0f, 1.0f }, .texcoord = { 1.0f, 0.0f }, .normal = { 0.0f, 0.0f, 1.0f } }); // 右上
	//modelData2.vertices.push_back({ .position = { -1.0f,  -1.0f, 0.0f, 1.0f }, .texcoord = { 0.0f, 1.0f }, .normal = { 0.0f, 0.0f, 1.0f } }); // 左下
	//modelData2.vertices.push_back({ .position = { -1.0f,  -1.0f, 0.0f, 1.0f }, .texcoord = { 0.0f, 1.0f }, .normal = { 0.0f, 0.0f, 1.0f } }); // 左下
	//modelData2.vertices.push_back({ .position = { 1.0f, 1.0f, 0.0f, 1.0f }, .texcoord = { 1.0f, 0.0f }, .normal = { 0.0f, 0.0f, 1.0f } }); // 右上
	//modelData2.vertices.push_back({ .position = { 1.0f, -1.0f, 0.0f, 1.0f }, .texcoord = { 1.0f, 1.0f }, .normal = { 0.0f, 0.0f, 1.0f } }); // 右下
	//modelData2.material.textureFilePath = "Resources/uvChecker.png";

	//uint32_t instanceCount = 10;==================================================================================================

	// Textureを2枚読み込んで転送
	DirectX::ScratchImage mipImages[2] = {
		LoadTexture("Resources/uvChecker.png"),
		LoadTexture(modelData.material.textureFilePath)
	};

	const DirectX::TexMetadata& metadata = mipImages[0].GetMetadata();
	const DirectX::TexMetadata& metadata2 = mipImages[1].GetMetadata();

	Microsoft::WRL::ComPtr<ID3D12Resource> textureResource[2] = {
		CreateTextureResource(dxCore->GetDevice(), metadata),
		CreateTextureResource(dxCore->GetDevice(), metadata2) };

	Microsoft::WRL::ComPtr<ID3D12Resource> intermediateResource[2] = {
		/*UploadTextureData(textureResource[0], mipImages[0], dxCore->GetDevice(), dxCore->GetCommandList()),
		UploadTextureData(textureResource[1], mipImages[1], dxCore->GetDevice(), dxCore->GetCommandList())*/ };


	// DepthStencilTextureをウィンドウサイズで作成
	Microsoft::WRL::ComPtr<ID3D12Resource> depthStencilResource = CreateDepthStencilTextureResource(dxCore->GetDevice(), WindowsApplication::kClientWidth, WindowsApplication::kClientHeight);

	//=========================
	// DescriptorHeapを生成する
	//=========================
	// RTV用のヒープでディスクリプタの数は2。RTVはShader内で触るものではないので、ShaderVisibleはfalse
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvDescriptorHeap = CreateDescriptorHeap(dxCore->GetDevice(), D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 2, false);

	// SRV用のヒープでディスクリプタの数は128。SRVはShader内で使うのでShaderVisibleはtrue
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> srvDescriptorHeap = CreateDescriptorHeap(dxCore->GetDevice(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 128, true);

	// DSV用のヒープでディスクリプタの数は1。DSVはShader内で触るものではないので、ShaderVisibleはfalse
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> dsvDescriptorHeap = CreateDescriptorHeap(dxCore->GetDevice(), D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1, false);

	// metaDataを基にSRVの設定
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc[2]{};
	srvDesc[0].Format = metadata.format;
	srvDesc[0].Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc[0].ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;// 2Dテクスチャ
	srvDesc[0].Texture2D.MipLevels = UINT(metadata.mipLevels);

	srvDesc[1].Format = metadata2.format;
	srvDesc[1].Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc[1].ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;// 2Dテクスチャ
	srvDesc[1].Texture2D.MipLevels = UINT(metadata2.mipLevels);

	// DSVの設定
	D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
	dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;// Format。基本的にはResourceに合わせる
	dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;//2DTexture

	// DepthStencilStateの設定
	D3D12_DEPTH_STENCIL_DESC depthStencilDesc{};
	// Depthの機能を有効化する
	depthStencilDesc.DepthEnable = true;
	// 書き込む
	depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
	// 比較関数はLessEqual。つまり、近ければ描画される。変更したい場合は3_1の22ページを参照
	depthStencilDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;

	const uint32_t descriptorSizeSRV = dxCore->GetDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	const uint32_t descriptorSizeRTV = dxCore->GetDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	const uint32_t descriptorSizeDSV = dxCore->GetDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);

	// SRVを作成するDescriptorHeapの場所を決める
	D3D12_CPU_DESCRIPTOR_HANDLE textureSrvHandleCPU[2] = {
		GetCPUDescriptorHandle(srvDescriptorHeap,descriptorSizeSRV,1),
		GetCPUDescriptorHandle(srvDescriptorHeap,descriptorSizeSRV,2)
	};

	D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandleGPU[2] = {
		GetGPUDescriptorHandle(srvDescriptorHeap,descriptorSizeSRV,1),
		GetGPUDescriptorHandle(srvDescriptorHeap,descriptorSizeSRV,2)
	};

	//// 先頭はImGUIが使用するのでその次から使う
	//// 1枚目
	//textureSrvHandleCPU[0].ptr += dxCore->GetDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	//textureSrvHandleGPU[0].ptr += dxCore->GetDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	//// 2枚目
	//textureSrvHandleCPU[1].ptr += dxCore->GetDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV) * 2;
	//textureSrvHandleGPU[1].ptr += dxCore->GetDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV) * 2;

	// SRVの生成
	dxCore->GetDevice()->CreateShaderResourceView(textureResource[0].Get(), &srvDesc[0], textureSrvHandleCPU[0]);
	dxCore->GetDevice()->CreateShaderResourceView(textureResource[1].Get(), &srvDesc[1], textureSrvHandleCPU[1]);

	// DSVHeapの先頭にDSVを作る
	dxCore->GetDevice()->CreateDepthStencilView(depthStencilResource.Get(), &dsvDesc, dsvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());

	if (FAILED(hr)) {
		std::cerr << "CreateSwapChainForHwnd failed: " << hr << std::endl; // エラーコードを出力
		if (hr == DXGI_ERROR_INVALID_CALL) {
			std::cerr << "DXGI_ERROR_INVALID_CALL: Invalid parameters passed to the function." << std::endl; // エラーコードの説明
		}
		// 他のエラーコードも同様にチェック
		assert(false);
	}

	// ディスクリプタヒープが作れなかったので起動できない
	assert(SUCCEEDED(hr));

	//　SwapChainからResourceを引っ張ってくる
	Microsoft::WRL::ComPtr<ID3D12Resource> swapChainResources[2] = { nullptr };
	hr = dxCore->GetSwapChain()->GetBuffer(0, IID_PPV_ARGS(&swapChainResources[0]));

	//上手く取得できなかったので起動できない
	assert(SUCCEEDED(hr));

	// 1もやる
	hr = dxCore->GetSwapChain()->GetBuffer(1, IID_PPV_ARGS(&swapChainResources[1]));

	//上手く取得できなかったので起動できない
	assert(SUCCEEDED(hr));

	// RTVの設定
	D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
	rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;      // 出力結果をSRGBに変換して書き込む
	rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D; // 2Dテクスチャとして書き込む

	// ディスクリプタの先頭を取得する
	D3D12_CPU_DESCRIPTOR_HANDLE rtvStartHandle = rtvDescriptorHeap->GetCPUDescriptorHandleForHeapStart();

	// RTVを2つ作るのでディスクリプタを2つ用い
	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandles[2];

	// まず1つ目を作る。1つ目は作るところを指定してあげる
	rtvHandles[0] = rtvStartHandle;
	dxCore->GetDevice()->CreateRenderTargetView(swapChainResources[0].Get(), &rtvDesc, rtvHandles[0]);

	// 2つ目のディスクリプタハンドルを取得
	rtvHandles[1].ptr = rtvHandles[0].ptr + dxCore->GetDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

	// 2つ目を作る
	dxCore->GetDevice()->CreateRenderTargetView(swapChainResources[1].Get(), &rtvDesc, rtvHandles[1]);

	

	//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
	// ここDirectX初期化終了位置
	//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

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

	//=============================
	// PipelineStateObject
	//=============================
	// RootSignature作成
	D3D12_ROOT_SIGNATURE_DESC descriptionRootSignature{};
	descriptionRootSignature.Flags =
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

	// RootParameterを作成。PixelShaderのMaterialとVertexShaderのTransform。テクスチャが[2]を使用
	D3D12_ROOT_PARAMETER rootParameters[4] = {};
	rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;     // CBVを使う。PSのb0のb
	rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;  // PixelShaderで使う
	rootParameters[0].Descriptor.ShaderRegister = 0;                     // レジスタ番号0。PSのb0の0
	rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;     // CBVを使う。VSのb0のb
	rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX; // VertexShaderで使う
	rootParameters[1].Descriptor.ShaderRegister = 0;                     // レジスタ番号0。VSのb0の0
	rootParameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;     // CBVを使う
	rootParameters[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;  // PixelShaderで使う
	rootParameters[3].Descriptor.ShaderRegister = 1;                     // レジスタ番号1を使う

	descriptionRootSignature.pParameters = rootParameters;               // ルートパラメータ配列へのポイント
	descriptionRootSignature.NumParameters = _countof(rootParameters);   // 配列の長さ

	Microsoft::WRL::ComPtr<ID3D12Resource> directionalLightResource = dxCore->CreateBufferResource(sizeof(DirectionalLight));
	DirectionalLight* directionalLightData = nullptr;
	directionalLightResource->Map(0, nullptr, reinterpret_cast<void**>(&directionalLightData));
	directionalLightData->color = { 1.0f,1.0f,1.0f,1.0f };
	directionalLightData->direction = { 0.0f,-1.0f,0.0f };
	directionalLightData->intensity = 1.0f;

	LightingMode lightingMode = LightingMode::None;


	// DescriptorRange
	D3D12_DESCRIPTOR_RANGE descriptorRange[1] = {};
	descriptorRange[0].BaseShaderRegister = 0;                      // 0から始まる
	descriptorRange[0].NumDescriptors = 1;                          // 数は1つ
	descriptorRange[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV; // SRVを使用
	descriptorRange[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND; // Offsetを自動設定

	// DescriptorTable
	rootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; // DescriptorTableを使用
	rootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL; // PixelShaderで使用
	rootParameters[2].DescriptorTable.pDescriptorRanges = descriptorRange; // Tableの中身の配列を指定
	rootParameters[2].DescriptorTable.NumDescriptorRanges = _countof(descriptorRange); // Tableで利用する数

	// DescriptorRangeこれインスタンシング用
	D3D12_DESCRIPTOR_RANGE descriptorRangeForInstancing[1] = {};
	descriptorRangeForInstancing[0].BaseShaderRegister = 0;                      // 0から始まる
	descriptorRangeForInstancing[0].NumDescriptors = 1;                          // 数は1つ
	descriptorRangeForInstancing[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV; // SRVを使用
	descriptorRangeForInstancing[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND; // Offsetを自動設定

	// DescriptorTable
	rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE; // DescriptorTableを使用
	rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX; // VertexShaderで使用
	rootParameters[1].DescriptorTable.pDescriptorRanges = descriptorRangeForInstancing; // Tableの中身の配列を指定
	rootParameters[1].DescriptorTable.NumDescriptorRanges = _countof(descriptorRangeForInstancing); // Tableで利用する数

	// Sumplerの設定。rootSignatureは532行付近にあると思う(上にコード追加してたら知らん)
	// 基本の設定なので暫くはこれで問題ない
	D3D12_STATIC_SAMPLER_DESC staticSamplers[1] = {};
	staticSamplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;         // バイリニアフィルタ？
	staticSamplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;       // 0~1の範囲をリピート
	staticSamplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;       // 
	staticSamplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;       // 
	staticSamplers[0].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;     // 比較しない
	staticSamplers[0].MaxLOD = D3D12_FLOAT32_MAX;                       // Mipmapをあるだけ使用
	staticSamplers[0].ShaderRegister = 0;                               // レジスタ番号0を使用
	staticSamplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL; // PixelShaderで使用
	descriptionRootSignature.pStaticSamplers = staticSamplers;
	descriptionRootSignature.NumStaticSamplers = _countof(staticSamplers);

	// シリアライズしてバイナリにする
	Microsoft::WRL::ComPtr<ID3DBlob> signatureBlob = nullptr;
	Microsoft::WRL::ComPtr<ID3DBlob>  errorBlob = nullptr;
	hr = D3D12SerializeRootSignature(&descriptionRootSignature,
		D3D_ROOT_SIGNATURE_VERSION_1, &signatureBlob, &errorBlob);
	if (FAILED(hr)) {
		Log(reinterpret_cast<char*>(errorBlob->GetBufferSize()));
		assert(false);
	}

	// バイナリをもとに生成
	Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature = nullptr;
	hr = dxCore->GetDevice()->CreateRootSignature(0,
		signatureBlob->GetBufferPointer(), signatureBlob->GetBufferSize(),
		IID_PPV_ARGS(&rootSignature));
	assert(SUCCEEDED(hr));

	// InputLayoutの設定
	D3D12_INPUT_ELEMENT_DESC inputElementDescs[3] = {};
	inputElementDescs[0].SemanticName = "POSITION";
	inputElementDescs[0].SemanticIndex = 0;
	inputElementDescs[0].Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
	inputElementDescs[0].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
	inputElementDescs[1].SemanticName = "TEXCOORD";
	inputElementDescs[1].SemanticIndex = 0;
	inputElementDescs[1].Format = DXGI_FORMAT_R32G32_FLOAT;
	inputElementDescs[1].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
	inputElementDescs[2].SemanticName = "NORMAL";
	inputElementDescs[2].SemanticIndex = 0;
	inputElementDescs[2].Format = DXGI_FORMAT_R32G32B32_FLOAT;
	inputElementDescs[2].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;
	D3D12_INPUT_LAYOUT_DESC inputLayoutDesc{};
	inputLayoutDesc.pInputElementDescs = inputElementDescs;
	inputLayoutDesc.NumElements = _countof(inputElementDescs);

	// BlendStateの設定
	D3D12_BLEND_DESC blendDesc{};

	// ImGuiでブレンドモードの変更できるようにしてあるけど初期化時点でやりたいときはここでやる
	BlendMode blendMode = kBlendModeNormal;

	// 全ての色要素を書き込む
	blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
	blendDesc.RenderTarget[0].BlendEnable = TRUE;
	blendDesc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;   // α値の設定だから基本使わない
	blendDesc.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD; // α値の設定だから基本使わない
	blendDesc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO; // α値の設定だから基本使わない

	// ここから条件分岐
	switch (blendMode)
	{
	case kBlendModeNone:
		break;
	case kBlendModeNormal:

		blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;      // 基本ここをいじる
		blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;          // 基本ここをいじる
		blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA; // 基本ここをいじる

		break;
	case kBlendModeAdd:

		blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;      // 基本ここをいじる
		blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;          // 基本ここをいじる
		blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;           // 基本ここをいじる

		break;
	case kBlendModeSubtract:

		blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;      // 基本ここをいじる
		blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_REV_SUBTRACT; // 基本ここをいじる
		blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;           // 基本ここをいじる

		break;
	case kBlendModeMultily:

		blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_ZERO;           // 基本ここをいじる
		blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;          // 基本ここをいじる
		blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_SRC_COLOR;     // 基本ここをいじる

		break;
	case kBlendModeScreen:

		blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_INV_DEST_COLOR; // 基本ここをいじる
		blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;          // 基本ここをいじる
		blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;           // 基本ここをいじる

		break;
	case kCountOfBlendMode:
		break;
	default:
		break;
	}

	// ResiterzerStateの設定
	D3D12_RASTERIZER_DESC rasterizerDesc{};

	// 裏面(時計回り)を表示しない
	rasterizerDesc.CullMode = D3D12_CULL_MODE_BACK;

	// 三角形の中を塗りつぶす
	rasterizerDesc.FillMode = D3D12_FILL_MODE_SOLID;

	// シェーダーをコンパイルする
	IDxcBlob* vertexShaderBlob = CompileShader(L"Resources/Shaders/Particle.VS.hlsl",
		L"vs_6_0", dxcUtils, dxcCompiler, includeHandler);
	assert(vertexShaderBlob != nullptr);

	IDxcBlob* pixelShaderBlob = CompileShader(L"Resources/Shaders/Particle.PS.hlsl",
		L"ps_6_0", dxcUtils, dxcCompiler, includeHandler);
	assert(vertexShaderBlob != nullptr);

	//===============
	//PSOを生成
	//===============
	D3D12_GRAPHICS_PIPELINE_STATE_DESC graphicsPipelineStateDesc{};
	graphicsPipelineStateDesc.pRootSignature = rootSignature.Get();
	graphicsPipelineStateDesc.InputLayout = inputLayoutDesc;
	graphicsPipelineStateDesc.VS = { vertexShaderBlob->GetBufferPointer(),
	vertexShaderBlob->GetBufferSize() };
	graphicsPipelineStateDesc.PS = { pixelShaderBlob->GetBufferPointer(),
	pixelShaderBlob->GetBufferSize() };
	graphicsPipelineStateDesc.BlendState = blendDesc;
	graphicsPipelineStateDesc.RasterizerState = rasterizerDesc;

	// 書き込むRTVの情報
	graphicsPipelineStateDesc.NumRenderTargets = 1;
	graphicsPipelineStateDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;

	// DepthStencilの設定
	graphicsPipelineStateDesc.DepthStencilState = depthStencilDesc;
	graphicsPipelineStateDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;

	// 利用するトポロジ(形状)のタイプ。三角形
	graphicsPipelineStateDesc.PrimitiveTopologyType =
		D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

	// どのように画面に色を打ち込むかの設定
	graphicsPipelineStateDesc.SampleDesc.Count = 1;
	graphicsPipelineStateDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;

	// 実際に生成
	Microsoft::WRL::ComPtr<ID3D12PipelineState> graphicsPipelineState = nullptr;
	hr = dxCore->GetDevice()->CreateGraphicsPipelineState(&graphicsPipelineStateDesc,
		IID_PPV_ARGS(&graphicsPipelineState));
	assert(SUCCEEDED(hr));

	//=========================
	// VertexResourceを生成
	//=========================
	// 球を配置するため*1024(頂点の数)にする
	//Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource = CreateBufferResource(device, sizeof(VertexData) * 1024);

	// 頂点リソースを作る
	Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource = dxCore->CreateBufferResource(sizeof(VertexData) * modelData.vertices.size());

	// 球のindexResource
	Microsoft::WRL::ComPtr<ID3D12Resource> indexResourceSphere = dxCore->CreateBufferResource(sizeof(uint32_t) * 1536);

	// sprite用の頂点リソースを作る
	//Microsoft::WRL::ComPtr<ID3D12Resource> vertexResourceSprite = dxCore->CreateBufferResource(sizeof(VertexData) * 4);

	// sprite用のIndexリソースを作る
	//Microsoft::WRL::ComPtr<ID3D12Resource> indexResourceSprite = dxCore->CreateBufferResource(sizeof(uint32_t) * 6);

	//=============================
	// Material用のResourceの作成
	//=============================
	// マテリアル用のリソースを作る。今回はMaterial1つ分のサイズを用意
	Microsoft::WRL::ComPtr<ID3D12Resource> materialResource = dxCore->CreateBufferResource(sizeof(Material));

	// スプライト用のマテリアルリソース
	//Microsoft::WRL::ComPtr<ID3D12Resource> materialResourceSprite = dxCore->CreateBufferResource(sizeof(Material));

	// マテリアルにデータを書き込む
	Material* materialData = nullptr;
	//Material* materialDataSprite = nullptr;

	// 書き込むためのアドレスを取得
	materialResource->Map(0, nullptr, reinterpret_cast<void**>(&materialData));
	//materialResourceSprite->Map(0, nullptr, reinterpret_cast<void**>(&materialDataSprite));

	// 白で書き込む
	materialData->color = Vector4(1.0f, 1.0f, 1.0f, 1.0f);
	//materialDataSprite->color = Vector4(1.0f, 1.0f, 1.0f, 1.0f);

	// 球のみLightingを有効にする
	materialData->enableLighting = true;
	//materialDataSprite->enableLighting = false;

	// UVTransformに単位行列を書き込む
	materialData->uvTransform = MakeIdentity4x4();
	//materialDataSprite->uvTransform = MakeIdentity4x4();

	// Sprite用のTransformationMatrix用のリソースを作る。Matrix4x4 1つ分のサイズを用意する
	//Microsoft::WRL::ComPtr<ID3D12Resource> transformationMatrixResourceSprite = dxCore->CreateBufferResource(sizeof(Matrix4x4) * 2);

	// データを書き込む
	//Matrix4x4* transformationMatrixDataSprite = nullptr;

	// 書き込むためのアドレスを取得
	//transformationMatrixResourceSprite->Map(0, nullptr, reinterpret_cast<void**>(&transformationMatrixDataSprite));

	// 単位行列を書き込んでおく
	//*transformationMatrixDataSprite = MakeIdentity4x4();

	//============================================
	// TransformationMatrix用のResourceの作成
	//============================================
	UINT transformationMatrixSize = (sizeof(TransformationMatrix) + 255) & ~255;

	// WVP用のリソースを作る。
	Microsoft::WRL::ComPtr<ID3D12Resource> wvpResource = dxCore->CreateBufferResource(transformationMatrixSize);

	// データを書き込む
	TransformationMatrix* transformationMatrix = nullptr;

	// 書き込むためのアドレスを取得
	wvpResource->Map(0, nullptr, reinterpret_cast<void**>(&transformationMatrix));

	// 単位行列を書き込む
	transformationMatrix->WVP = MakeIdentity4x4();
	transformationMatrix->World = MakeIdentity4x4();

	//=====================================
	// Instancing用のResourceの作成
	//=====================================

	//const uint32_t kNumInstance = 10;

	//// Instancing用のTransformationMatriResourceの作成
	//Microsoft::WRL::ComPtr<ID3D12Resource>instancingResource =
	//	dxCore->CreateBufferResource(sizeof(TransformationMatrix) * kNumInstance);

	////TransformationMatrix* gTransformationMatrice[10];

	//// 書き込むためのアドレスを取得
	//TransformationMatrix* instancingData = nullptr;
	//instancingResource->Map(0, nullptr, reinterpret_cast<void**>(&instancingData));

	//// 単位行列を書き込む
	//for (uint32_t index = 0; index < kNumInstance; ++index) {
	//	instancingData[index].WVP = MakeIdentity4x4();
	//	instancingData[index].World = MakeIdentity4x4();
	//}

	////========================================
 //   // Instancing用のSRVの作成
 //   //========================================
 //   // SRV（Shader Resource View）設定
	//D3D12_SHADER_RESOURCE_VIEW_DESC instancingSrvDesc{};
	//instancingSrvDesc.Format = DXGI_FORMAT_UNKNOWN; // 構造体のレイアウトは自由なので不明（UNKNOWN）
	//instancingSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	//instancingSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER; // StructuredBuffer用
	//instancingSrvDesc.Buffer.FirstElement = 0; // 最初の要素から
	//instancingSrvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE; // 固定
	//instancingSrvDesc.Buffer.NumElements = kNumInstance; // インスタンス数（アクセスする要素数）
	//instancingSrvDesc.Buffer.StructureByteStride = sizeof(TransformationMatrix); // 構造体1個のサイズ

	//// SRVハンドルを取得（Heapの3番目に作成している例、空いていれば他でも可）
	//D3D12_CPU_DESCRIPTOR_HANDLE instancingSrvHandleCPU =
	//	GetCPUDescriptorHandle(srvDescriptorHeap, descriptorSizeSRV, 3);
	//D3D12_GPU_DESCRIPTOR_HANDLE instancingSrvHandleGPU =
	//	GetGPUDescriptorHandle(srvDescriptorHeap, descriptorSizeSRV, 3);

	//// SRVの作成
	//dxCore->GetDevice()->CreateShaderResourceView(
	//	instancingResource.Get(),
	//	&instancingSrvDesc,
	//	instancingSrvHandleCPU
	//);

	//===================================
	// BufferViewの作成
	//===================================
	// 頂点バッファビューを作成
	D3D12_VERTEX_BUFFER_VIEW vertexBufferView{};

	// リソースの先頭アドレスから使用
	vertexBufferView.BufferLocation = vertexResource->GetGPUVirtualAddress();

	// 使用するリソースのサイズは頂点1024個のサイズ
	//vertexBufferView.SizeInBytes = sizeof(VertexData) * 1024;
	vertexBufferView.SizeInBytes = UINT(sizeof(VertexData) * modelData.vertices.size());

	// 1頂点当たりのサイズ
	vertexBufferView.StrideInBytes = sizeof(VertexData);

	// sprite用の頂点バッファビューを作成
	//D3D12_VERTEX_BUFFER_VIEW vertexBufferViewSprite{};

	// Spere用のindexバッファビューを作る
	D3D12_INDEX_BUFFER_VIEW indexBufferViewSphere{};
	indexBufferViewSphere.BufferLocation = indexResourceSphere->GetGPUVirtualAddress();
	indexBufferViewSphere.SizeInBytes = sizeof(uint32_t) * 1536;
	indexBufferViewSphere.Format = DXGI_FORMAT_R32_UINT;

	// リソースの先頭アドレスから使用
	//vertexBufferViewSprite.BufferLocation = vertexResourceSprite->GetGPUVirtualAddress();

	// 使用するリソースのサイズは頂点4個のサイズ
	//vertexBufferViewSprite.SizeInBytes = sizeof(VertexData) * 4;

	// 1頂点あたりのサイズ
	//vertexBufferViewSprite.StrideInBytes = sizeof(VertexData);

	// sprite用のindexバッファビューを作る
	//D3D12_INDEX_BUFFER_VIEW indexBufferViewSprite{};

	// リソースの先頭アドレスから使う
	//indexBufferViewSprite.BufferLocation = indexResourceSprite->GetGPUVirtualAddress();

	// 使用するリソースのサイズはインデックスの6つ分のサイズ
	//indexBufferViewSprite.SizeInBytes = sizeof(uint32_t) * 6;

	// インデックスはuint32_tとする
	//indexBufferViewSprite.Format = DXGI_FORMAT_R32_UINT;

	// 板ポリ頂点バッファを作成========================================================================================
	/*Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource2 =
		dxCore->CreateBufferResource(sizeof(VertexData) * modelData2.vertices.size());*/

	// 頂点データ転送
	/*VertexData* vertexData2 = nullptr;
	vertexResource2->Map(0, nullptr, reinterpret_cast<void**>(&vertexData2));
	std::memcpy(vertexData2, modelData2.vertices.data(), sizeof(VertexData) * modelData2.vertices.size());
	vertexResource2->Unmap(0, nullptr);*/

	// ビュー作成
	/*D3D12_VERTEX_BUFFER_VIEW vertexBufferView2{};
	vertexBufferView2.BufferLocation = vertexResource2->GetGPUVirtualAddress();
	vertexBufferView2.SizeInBytes = UINT(sizeof(VertexData) * modelData2.vertices.size());
	vertexBufferView2.StrideInBytes = sizeof(VertexData);*/
	//====================================================================================================================

	//============================
	// Resourceにデータを書き込む
	//============================
	// 頂点リソースにデータを書き込む
	VertexData* vertexData = nullptr;

	// 書き込むためのアドレスを取得
	vertexResource->Map(0, nullptr, reinterpret_cast<void**>(&vertexData));

	std::memcpy(vertexData, modelData.vertices.data(), sizeof(VertexData) * modelData.vertices.size());

	// sprite用の頂点データ
	//VertexData* vertexDataSprite = nullptr;
	//vertexResourceSprite->Map(0, nullptr, reinterpret_cast<void**>(&vertexDataSprite));

	// indexに格納するから同一頂点のデータをわざわざ用意する必要はない
	// 左下
	//vertexDataSprite[0].position = { 0.0f,360.0f,0.0f,1.0f };
	//vertexDataSprite[0].texcoord = { 0.0f,1.0f };
	//vertexDataSprite[0].normal = { 0.0f,0.0f,-1.0f };

	//// 左上
	//vertexDataSprite[1].position = { 0.0f,0.0f,0.0f,1.0f };
	//vertexDataSprite[1].texcoord = { 0.0f,0.0f };
	//vertexDataSprite[1].normal = { 0.0f,0.0f,-1.0f };

	//// 右下
	//vertexDataSprite[2].position = { 640.0f,360.0f,0.0f,1.0f };
	//vertexDataSprite[2].texcoord = { 1.0f,1.0f };
	//vertexDataSprite[2].normal = { 0.0f,0.0f,-1.0f };

	//// 右上
	//vertexDataSprite[3].position = { 640.0f,0.0f,0.0f,1.0f };
	//vertexDataSprite[3].texcoord = { 1.0f,0.0f };
	//vertexDataSprite[3].normal = { 0.0f,0.0f,-1.0f };

	// インデックスリソースにデータを書き込む
	/*uint32_t* indexDataSprite = nullptr;
	indexResourceSprite->Map(0, nullptr, reinterpret_cast<void**>(&indexDataSprite));
	indexDataSprite[0] = 0;
	indexDataSprite[1] = 1;
	indexDataSprite[2] = 2;
	indexDataSprite[3] = 1;
	indexDataSprite[4] = 3;
	indexDataSprite[5] = 2;*/

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

	Transform transform{ {1.0f,1.0f,1.0f},{0.0f,0.0f,0.0f},{0.0f,0.0f,0.0f} };
	Transform cameraTransform{ {1.0f,1.0f,1.0f},{0.0f,0.0f,0.0f},{0.0f,0.0f,-10.0f} };
	Transform transformSprite{ {1.0f,1.0f,1.0f},{0.0f,0.0f,0.0f},{0.0f,0.0f,0.0f} };
	Transform uvTransformSprite{ {1.0f,1.0f,1.0f},{0.0f,0.0f,0.0f},{0.0f,0.0f,0.0f} };

	//=====================================
	// Instancing用のTransformの作成と書き込み
	//=====================================
	//ここ
	/*Transform transforms[kNumInstance];
	for (uint32_t index = 0; index < kNumInstance; ++index) {
		transforms[index].scale = { 1.0f,1.0f,1.0f };
		transforms[index].rotate = { 0.0f,0.0f,0.0f };
		transforms[index].translate = { index*0.1f,index * 0.1f,index * 0.1f };
	}*/

	// サウンドを再生
	// キー入力ができるようになったらキー入力で再生とかの方が望ましい
	//SoundPlayWave(xAudio2.Get(), soundData);

	//================
	// ImGUIの初期化
	//================
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGui::StyleColorsDark();
	ImGui_ImplWin32_Init(winApp->GetHwnd());
	ImGui_ImplDX12_Init(dxCore->GetDevice(),
		dxCore->GetBackBufferCount(),
		rtvDesc.Format,
		srvDescriptorHeap.Get(),
		srvDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
		srvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());

	bool useMonsterBall = true;
	bool isSpriteVisible = true;
	int isUseDebugCamera = true;
	int isUseHalfLambert = true;

	//MSG msg{};
	// ウィンドウのxボタンが押されるまでループ
	while (/*msg.message != WM_QUIT*/winApp->ProcessMessage()) {

		// ImGUIにここから始まることを通知
		ImGui_ImplDX12_NewFrame();
		ImGui_ImplWin32_NewFrame();
		ImGui::NewFrame();

		///
		// ここからゲームの処理
		///

		//=========================
		// キーボードの入力処理
		//=========================
		// 更新
		keyboardInput->Update();

		// 数字の0のキーが押されていたら
		if (keyboardInput->TriggerKey(DIK_0)) {
			OutputDebugStringA("trigger [0]\n");
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
		ImGui::Begin("Triangle Settings");

		// ModelTransform
		ImGui::Text("ModelTransform");
		ImGui::DragFloat3("Scale", &transform.scale.x, 0.01f);
		ImGui::DragFloat3("Rotate", &transform.rotate.x, 0.01f);
		ImGui::DragFloat3("Translate", &transform.translate.x, 1.0f);
		ImGui::ColorEdit4("ColorEdit", &materialData->color.x);

		//==============================
		// ここからブレンドモードの変更処理
		//==============================
		// BlendMode 選択 UI
		const char* blendItems[] = {
			"None",
			"Normal",
			"Add",
			"Subtract",
			"Multiply",
			"Screen"
		};

		int currentBlendIndex = static_cast<int>(blendMode);
		if (ImGui::Combo("Blend Mode", &currentBlendIndex, blendItems, IM_ARRAYSIZE(blendItems))) {
			blendMode = static_cast<BlendMode>(currentBlendIndex);

			// --- PSO 再生成 ---
			D3D12_BLEND_DESC newBlendDesc{};
			newBlendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
			newBlendDesc.RenderTarget[0].BlendEnable = TRUE;

			switch (blendMode) {
			case kBlendModeNone:
				newBlendDesc.RenderTarget[0].BlendEnable = FALSE;
				break;
			case kBlendModeNormal:
				newBlendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
				newBlendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
				newBlendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
				break;
			case kBlendModeAdd:
				newBlendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
				newBlendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
				newBlendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
				break;
			case kBlendModeSubtract:
				newBlendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
				newBlendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_REV_SUBTRACT;
				newBlendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
				break;
			case kBlendModeMultily:
				newBlendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_ZERO;
				newBlendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
				newBlendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_SRC_COLOR;
				break;
			case kBlendModeScreen:
				newBlendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_INV_DEST_COLOR;
				newBlendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
				newBlendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
				break;
			}

			newBlendDesc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
			newBlendDesc.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
			newBlendDesc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;

			// PSO の設定をコピーして再作成
			D3D12_GRAPHICS_PIPELINE_STATE_DESC newDesc = graphicsPipelineStateDesc;
			newDesc.BlendState = newBlendDesc;

			Microsoft::WRL::ComPtr<ID3D12PipelineState> newPSO;
			HRESULT hr = dxCore->GetDevice()->CreateGraphicsPipelineState(&newDesc, IID_PPV_ARGS(&newPSO));
			assert(SUCCEEDED(hr));
			graphicsPipelineState = newPSO; // 新しいPSOに差し替え
		}

		//==============================
		// ここまでブレンドモードの変更処理
		//==============================

		// SpriteTransform（Sprite自体の見た目位置など）
		ImGui::Checkbox("Show Sprite", &isSpriteVisible);
		ImGui::Text("SpriteTransform");
		ImGui::DragFloat3("Scale##Sprite", &transformSprite.scale.x, 0.01f);
		ImGui::DragFloat3("Rotate##Sprite", &transformSprite.rotate.x, 0.01f);
		ImGui::DragFloat3("Translate##Sprite", &transformSprite.translate.x, 1.0f);

		// UVTransform（テクスチャの位置・回転など）
		ImGui::Text("UVTransform");
		ImGui::DragFloat2("Scale##UV", &uvTransformSprite.scale.x, 0.01f);
		ImGui::SliderAngle("Rotate##UV", &uvTransformSprite.rotate.z);
		ImGui::DragFloat2("Translate##UV", &uvTransformSprite.translate.x, 0.01f);

		// カメラ操作
		ImGui::Text("DebugCamera");
		ImGui::Checkbox("Use Debug Camera", reinterpret_cast<bool*>(&isUseDebugCamera));

		// Light
		ImGui::Text("Light");

		const char* lightingItems[] = { "None", "Lambert", "Half Lambert" };
		int currentLightingIndex = static_cast<int>(lightingMode);
		if (ImGui::Combo("Lighting", &currentLightingIndex, lightingItems, IM_ARRAYSIZE(lightingItems))) {
			lightingMode = static_cast<LightingMode>(currentLightingIndex);
		}

		//ImGui::Checkbox("Use Half Lambert", reinterpret_cast<bool*>(&isUseHalfLambert));
		ImGui::ColorEdit4("Color", &directionalLightData->color.x);
		ImGui::DragFloat3("Direction", &directionalLightData->direction.x, 0.01f);
		ImGui::DragFloat("Intensity", &directionalLightData->intensity, 0.01f);

		ImGui::End();

		//===================================
		// デバッグカメラの操作説明(別ウィンドウ)
		//===================================
		ImGui::Begin("Debug Camera Controls");

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

		ImGui::End();

		debugCamera->Update(keyboardInput->keys_);

		// PSにどのライティング方式を使用するかを送る
		directionalLightData->lightingType = static_cast<int>(lightingMode);

		Matrix4x4 worldMatrix = MakeAffineMatrix(transform);
		Matrix4x4 cameraMatrix = MakeAffineMatrix(cameraTransform);
		Matrix4x4 viewMatrix;

		// デバッグカメラと通常カメラの切り替え
		if (isUseDebugCamera) {
			viewMatrix = debugCamera->GetViewMatrix();

		} else {
			viewMatrix = Inverse(cameraMatrix);

		}

		Matrix4x4 projectionMatrix = MakePerspectiveFovMatrix(0.45f, 16.0f / 9.0f, 0.1f, 100.0f);
		Matrix4x4 worldViewProjectionMatrix = Multiply(worldMatrix, Multiply(viewMatrix, projectionMatrix));
		//transformationMatrix->World = worldMatrix;
		//transformationMatrix->WVP = worldViewProjectionMatrix;

		// sprite用のworldViewProjectionMatrixを作る
		/*Matrix4x4 worldMatrixSprite = MakeAffineMatrix(transformSprite);
		Matrix4x4 viewMatrixSprite = MakeIdentity4x4();
		Matrix4x4 projectionMatrixSprite = MakeOrthographicMatrix(0.0f, 0.0f, WindowsApplication::kClientWidth, WindowsApplication::kClientHeight, 0.0f, 100.0f);
		Matrix4x4 worldViewProjectionMatrixSprite = Multiply(worldMatrixSprite, Multiply(viewMatrixSprite, projectionMatrixSprite));
		*transformationMatrixDataSprite = worldViewProjectionMatrixSprite;*/

		//UVTransform
		Matrix4x4 uvTransformMatrix = MakeScaleMatrix(uvTransformSprite);
		uvTransformMatrix = Multiply(uvTransformMatrix, MakeRotateZMatrix(uvTransformSprite.rotate));
		uvTransformMatrix = Multiply(uvTransformMatrix, MakeTranslateMatrix(uvTransformSprite));
		//materialDataSprite->uvTransform = uvTransformMatrix;

		// Instancing
		// WVP等を計算してResourceに書き込む
		//for (uint32_t index = 0; index < kNumInstance; ++index) {
		//	// ワールド行列作成
		//	Matrix4x4 worldMatrixInstancing = MakeAffineMatrix(transforms[index]);

		//	Matrix4x4 viewMatrixInstancing;

		//	if (isUseDebugCamera) {
		//		viewMatrixInstancing = debugCamera->GetViewMatrix();

		//	} else {
		//		viewMatrixInstancing = Inverse(cameraMatrix);

		//	}

		//	Matrix4x4 viewProjectionMatrixInstancing = Multiply(viewMatrixInstancing, projectionMatrix);

		//	// WVP行列作成
		//	Matrix4x4 worldViewProjectionMatrixInstancing = Multiply(worldMatrixInstancing, viewProjectionMatrixInstancing);

		//	// 書き込み
		//	instancingData[index].WVP = worldViewProjectionMatrixInstancing;
		//	instancingData[index].World = worldMatrixInstancing;
		//}

		// 回転テスト
		/*float rotation = sprite->GetRotation();
		rotation += 0.01f;
		sprite->SetRotation(rotation);*/

		// 移動テスト
		Vector2 position = sprite->GetPosition();
		position.x += 0.1f;
		position.y += 0.1f;
		sprite->SetPosition(position);

		// 色変更テスト
		/*Vector4 color = sprite->GetColor();
		color.x += 0.01f;
		if (color.x > 1.0f) {
			color.x -= 1.0f;
		}
		sprite->SetColor(color);*/

		// サイズ変更テスト
		/*Vector2 size = sprite->GetSize();
		size.x += 0.1f;
		size.y += 0.1f;
		sprite->SetSize(size);*/

		sprite->Update();

		for (SpriteInstance* sprite : sprites) {
			sprite->Update();
			Log("updateCount\n");
		}

		// ESCAPEキーのトリガー入力で終了
		if (keyboardInput->TriggerKey(DIK_ESCAPE)) {
			PostQuitMessage(0);
		}

		///
		// ここまでゲームの処理
		///

		// ゲームの処理が終わったので描画処理前にImGUIの内部コマンドを生成
		ImGui::Render();

		dxCore->BeginDraw();

		// SRVヒープをコマンドリストに設定
		ID3D12DescriptorHeap* heaps[] = { srvDescriptorHeap.Get() };
		dxCore->GetCommandList()->SetDescriptorHeaps(_countof(heaps), heaps);

		//=========================
		// コマンドを積む
		//=========================
		dxCore->GetCommandList()->RSSetViewports(1, &viewport);                // Viewportを設定
		dxCore->GetCommandList()->RSSetScissorRects(1, &scissorRect);          // Scirssorを設定

		// RootSignatureを設定。PSOに設定してるけど別途設定が必要
		dxCore->GetCommandList()->SetGraphicsRootSignature(rootSignature.Get());
		dxCore->GetCommandList()->SetPipelineState(graphicsPipelineState.Get());     // PSOを設定
		dxCore->GetCommandList()->IASetVertexBuffers(0, 1, &vertexBufferView); // VBVを設定
		dxCore->GetCommandList()->IASetIndexBuffer(&indexBufferViewSphere);    // IBVを設定

		// 形状を設定。PSOに設定しているものとは別。同じものを設定。
		dxCore->GetCommandList()->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		// 指定した深度で画面全体をクリアする
		dxCore->GetCommandList()->ClearDepthStencilView(dxCore->GetDsvHeap()->GetCPUDescriptorHandleForHeapStart(), D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

		// マテリアルCBufferの場所を設定
		dxCore->GetCommandList()->SetGraphicsRootConstantBufferView(0, materialResource->GetGPUVirtualAddress());

		// wvp用のCBufferの場所を設定
		// instancingResource は StructuredBuffer なので SRVテーブルとして設定する
		//dxCore->GetCommandList()->SetGraphicsRootDescriptorTable(1, instancingSrvHandleGPU);

		//dxCore->GetCommandList()->SetGraphicsRootConstantBufferView(1, instancingResource->GetGPUVirtualAddress());

		// SRVのDescriptorTableの先頭を設定。2はrootParameter[2]。useMonsterBallでテクスチャを切り替え
		dxCore->GetCommandList()->SetGraphicsRootDescriptorTable(2, useMonsterBall ? textureSrvHandleGPU[1] : textureSrvHandleGPU[0]);

		// rootParameter[3]のところにライトのリソースを入れる。CPUに送る
		dxCore->GetCommandList()->SetGraphicsRootConstantBufferView(3, directionalLightResource->GetGPUVirtualAddress());

		// 描画(drawCall/ドローコール)。3頂点で1つのインスタンス。
		// 1つ目の引数は頂点の数 球は1536
		//dxCore->GetCommandList()->DrawInstanced(UINT(modelData.vertices.size()), 1, 0, 0);
		//commandList->DrawIndexedInstanced(1536, 1, 0, 0, 0);

		// 板ポリを使うようセット
		//dxCore->GetCommandList()->IASetVertexBuffers(0, 1, &vertexBufferView2);

		// 板ポリ用の SRV（uvChecker.png の方）を設定
		//dxCore->GetCommandList()->SetGraphicsRootDescriptorTable(1, instancingSrvHandleGPU);

		// 板ポリをインスタンシング描画
		//dxCore->GetCommandList()->DrawInstanced(UINT(modelData2.vertices.size()), kNumInstance, 0, 0);


		if (isSpriteVisible) {

			//// マテリアルCBufferの場所を設定.Spriteの前に設定しなおす
			//dxCore->GetCommandList()->SetGraphicsRootConstantBufferView(0, materialResourceSprite->GetGPUVirtualAddress());

			//// spriteの描画。変更が必要なものだけ変更する。
			//dxCore->GetCommandList()->IASetVertexBuffers(0, 1, &vertexBufferViewSprite);

			////IBVを設定
			//dxCore->GetCommandList()->IASetIndexBuffer(&indexBufferViewSprite);

			//// sprite用のCBufferの場所の設定
			//dxCore->GetCommandList()->SetGraphicsRootConstantBufferView(1, transformationMatrixResourceSprite->GetGPUVirtualAddress());

			//// spriteの描画(drawCall/ドローコール)。vertexは4だけど格納したインデックスは6でインデックスで描画
			//dxCore->GetCommandList()->DrawIndexedInstanced(6, 1, 0, 0, 0);
		}

		spriteManager->DrawSetting();
		sprite->Draw();

		for (SpriteInstance* sprite : sprites) {
			Log("drawCount\n");
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
	ImGui_ImplDX12_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();

	// 入力を解放
	delete keyboardInput;

	// XAudio2解放
	xAudio2.Reset();

	// 音声データ解放
	// 絶対にXAudio2を解放してから行うこと
	SoundUnload(&soundData);

	//

	// スプライト解放
	for (SpriteInstance* sprite : sprites) {
		delete sprite;
	}
	sprites.clear();
	delete sprite;
	delete spriteManager;

	// dxc関連
	includeHandler->Release();
	dxcCompiler->Release();
	dxcUtils->Release();

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

float Cotangent(float theta)
{
	float cotngent;

	cotngent = 1.0f / tanf(theta);

	return cotngent;
}

IDxcBlob* CompileShader(const std::wstring& filePath, const wchar_t* profile, IDxcUtils* dxcUtils, IDxcCompiler3* dxcCompiler, IDxcIncludeHandler* includeHandler)
{
	//=========================
	// 1.hlslファイルを読み込む
	//=========================
	// これからシェーダをコンパイルする旨をログに出す
	Log(ConvertString(std::format(L"Begin CompileShader,path:{},profile:{}\n", filePath, profile)));

	// hlslファイルを読み込む
	IDxcBlobEncoding* shaderSource = nullptr;
	HRESULT hr = dxcUtils->LoadFile(filePath.c_str(), nullptr, &shaderSource);

	// 読めなかったら止める
	assert(SUCCEEDED(hr));

	// 読み込んだファイルの内容を設定する
	DxcBuffer shaderSourceBuffer;
	shaderSourceBuffer.Ptr = shaderSource->GetBufferPointer();
	shaderSourceBuffer.Size = shaderSource->GetBufferSize();
	shaderSourceBuffer.Encoding = DXC_CP_UTF8;// utf-8の文字コードであることを通知

	//==========================
	// Compileする
	//==========================
	LPCWSTR arguments[] = {
		filePath.c_str(),        // コンパイル対象のhlslファイル名
		L"-E",L"main",           // エントリーポイントの指定。基本的にmain以外にしない
		L"-T",profile,           // ShaderProfileの設定
		L"-Zi",L"-Qembed_debug", // デバッグ用の情報を埋め込む
		L"-Od",                  // 最適化を外しておく
		L"-Zpr",                 // メモリレイアウトは"行"優先
	};

	// 実際にShaderをコンパイル
	IDxcResult* shaderResult = nullptr;
	hr = dxcCompiler->Compile(
		&shaderSourceBuffer,        // 読み込んだファイル
		arguments,                  // コンパイルオプション
		_countof(arguments),        // コンパイルオプションの数
		includeHandler,             // インクルードが含まれた諸々
		IID_PPV_ARGS(&shaderResult) // コンパイル結果
	);

	// コンパイルエラーではなくdxcが起動できないなど致命的な状況で停止
	assert(SUCCEEDED(hr));

	//=============================
	// 警告、エラーが出ていないか確認
	//=============================
	// 警告、エラーが出たらログに出力し停止
	IDxcBlobUtf8* shaderError = nullptr;
	shaderResult->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&shaderError), nullptr);
	if (shaderError != nullptr && shaderError->GetStringLength() != 0) {

		// ログを出力
		Log(shaderError->GetStringPointer());

		// 停止
		assert(false);// ここで停止----------------------------------------------------------------------
	}

	//==============================
	// Compile結果を受け取って返す
	//==============================
	// コンパイル結果から実行用のバイナリ部分を取得
	IDxcBlob* shaderBlob = nullptr;
	hr = shaderResult->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&shaderBlob), nullptr);
	assert(SUCCEEDED(hr));

	// 成功したログを出力
	Log(ConvertString(std::format(L"Compile Succeeded, path{},profile:{}\n", filePath, profile)));

	// 使用済みリソースの解放
	shaderSource->Release();
	shaderResult->Release();

	// 実行用のバイナリを返却
	return shaderBlob;

}

//Microsoft::WRL::ComPtr<ID3D12Resource> CreateBufferResource(Microsoft::WRL::ComPtr<ID3D12Device> device, size_t sizeInBytes) {
//	//=========================
//	// VertexResourceを生成
//	//=========================
//	// 頂点リソース用のヒープ設定
//	D3D12_HEAP_PROPERTIES uploadHeapProperties{};
//	uploadHeapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;
//
//	// 頂点リソースの設定
//	D3D12_RESOURCE_DESC vertexResourceDesc{};
//
//	// バッファリソース。テクスチャの場合はまた別の設定をする
//	vertexResourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
//	vertexResourceDesc.Width = sizeInBytes;// リソースのサイズ。
//
//	// バッファの場合はこれらを1にする決まり
//	vertexResourceDesc.Height = 1;
//	vertexResourceDesc.DepthOrArraySize = 1;
//	vertexResourceDesc.MipLevels = 1;
//	vertexResourceDesc.SampleDesc.Count = 1;
//
//	// バッファの場合の儀式
//	vertexResourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
//
//	// 実際に頂点リソースを作る
//	Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource = nullptr;
//
//	HRESULT hr = device->CreateCommittedResource(&uploadHeapProperties, D3D12_HEAP_FLAG_NONE,
//		&vertexResourceDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
//		IID_PPV_ARGS(&vertexResource));
//
//	assert(SUCCEEDED(hr));
//
//	return vertexResource;
//}

Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> CreateDescriptorHeap(Microsoft::WRL::ComPtr<ID3D12Device> device, D3D12_DESCRIPTOR_HEAP_TYPE heapType, UINT numDescriptor, bool shaderVicible)
{
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> descriptorHeap = nullptr;
	D3D12_DESCRIPTOR_HEAP_DESC descriptorHeapDesc{};
	descriptorHeapDesc.Type = heapType;
	descriptorHeapDesc.NumDescriptors = numDescriptor;
	descriptorHeapDesc.Flags = shaderVicible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	HRESULT hr = device->CreateDescriptorHeap(&descriptorHeapDesc, IID_PPV_ARGS(&descriptorHeap));
	assert(SUCCEEDED(hr));

	return descriptorHeap;
}

//============================
// テクスチャの関数
//============================

DirectX::ScratchImage LoadTexture(const std::string& filePath)
{
	// テクスチャファイルを読み込んでプログラムで扱えるようにする
	DirectX::ScratchImage image{};
	std::wstring filePathW = ConvertString(filePath);
	HRESULT hr = DirectX::LoadFromWICFile(
		filePathW.c_str(), DirectX::WIC_FLAGS_FORCE_SRGB, nullptr, image);
	assert(SUCCEEDED(hr));

	// ミップマップの作成
	DirectX::ScratchImage mipImages{};
	hr = DirectX::GenerateMipMaps(
		image.GetImages(), image.GetImageCount(), image.GetMetadata(),
		DirectX::TEX_FILTER_SRGB, 0, mipImages);
	assert(SUCCEEDED(hr));

	// ミップマップ付きのデータを返す
	return mipImages;
}

Microsoft::WRL::ComPtr<ID3D12Resource> CreateTextureResource(Microsoft::WRL::ComPtr<ID3D12Device> device, const DirectX::TexMetadata& metadata)
{
	// metadataを基にResourceの設定
	D3D12_RESOURCE_DESC resourceDesc{};
	resourceDesc.Width = UINT(metadata.width);                             // Textureの横幅
	resourceDesc.Height = UINT(metadata.height);                           // Textureの縦幅
	resourceDesc.MipLevels = UINT16(metadata.mipLevels);                   // mipmapの数
	resourceDesc.DepthOrArraySize = UINT16(metadata.arraySize);            // 奥行or配列Textureの配列数
	resourceDesc.Format = metadata.format;                                 // TextureのFormat
	resourceDesc.SampleDesc.Count = 1;                                     // サンプリングカウント。1固定
	resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION(metadata.dimension); // Textureの次元数

	// 利用するHeapの設定。非常に特殊な運用--------------------------------------------
	D3D12_HEAP_PROPERTIES heapProperties{};
	heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;                       // Heap設定をVRAM上に作成
	//heapProperties.Type = D3D12_HEAP_TYPE_CUSTOM;                        // 細かい設定を行う
	//heapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_WRITE_BACK; // WriteBackポリシーでCPUアクセス可能
	//heapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_L0;          // プロセッサの近くに配置

	// Resourceを生成
	Microsoft::WRL::ComPtr<ID3D12Resource> resource = nullptr;
	HRESULT hr = device->CreateCommittedResource(
		&heapProperties,                   // Heapの設定
		D3D12_HEAP_FLAG_NONE,              // Heapの特殊な設定。無し
		&resourceDesc,                     // Resourceの設定
		D3D12_RESOURCE_STATE_COPY_DEST,    // データ転送される設定
		nullptr,                           // Clear最適値。使わない
		IID_PPV_ARGS(&resource));          // 作成するResourceポインタへのポインタ
	assert(SUCCEEDED(hr));

	return resource;
}

void UploadTextureData(Microsoft::WRL::ComPtr<ID3D12Resource> texture, const DirectX::ScratchImage& mipImages)
{
	// Mate情報を取得
	const DirectX::TexMetadata& metadata = mipImages.GetMetadata();

	// 全MipMapについて
	for (size_t mipLevel = 0; mipLevel < metadata.mipLevels; ++mipLevel) {

		// MipLevelを指定して各Imageを取得
		const DirectX::Image* img = mipImages.GetImage(mipLevel, 0, 0);

		// Textureに転送
		HRESULT hr = texture->WriteToSubresource(
			UINT(mipLevel),
			nullptr,              // 全領域へコピー
			img->pixels,          // 元データアドレス
			UINT(img->rowPitch),  // 1ラインのサイズ
			UINT(img->slicePitch) // 1枚のサイズ
		);

		assert(SUCCEEDED(hr));
	}

}

//Microsoft::WRL::ComPtr<ID3D12Resource> UploadTextureData(Microsoft::WRL::ComPtr<ID3D12Resource> texture, const DirectX::ScratchImage& mipImages, Microsoft::WRL::ComPtr<ID3D12Device> device, ID3D12GraphicsCommandList* commandList)
//{
//	std::vector<D3D12_SUBRESOURCE_DATA> subresources;
//	DirectX::PrepareUpload(device.Get(), mipImages.GetImages(), mipImages.GetImageCount(), mipImages.GetMetadata(), subresources);
//	uint64_t intermediateSize = GetRequiredIntermediateSize(texture.Get(), 0, UINT(subresources.size()));
//	Microsoft::WRL::ComPtr<ID3D12Resource> intermediateResource = dxCore_->CreateBufferResource(intermediateSize);
//	UpdateSubresources(commandList, texture.Get(), intermediateResource.Get(), 0, 0, UINT(subresources.size()), subresources.data());
//
//	// Textureへの転送後は利用できるよう、D3D12_RESOURCE_STATE_COPY_DESTからD3D12_RESOURCE_STATE_GENERIC_READへResourceStateを変更
//	D3D12_RESOURCE_BARRIER barrier{};
//	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
//	barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
//	barrier.Transition.pResource = texture.Get();
//	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
//	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
//	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_GENERIC_READ;
//	commandList->ResourceBarrier(1, &barrier);
//
//	return intermediateResource.Get();
//}

Microsoft::WRL::ComPtr<ID3D12Resource> CreateDepthStencilTextureResource(Microsoft::WRL::ComPtr<ID3D12Device> device, int32_t width, int32_t height)
{
	// 生成するResourceの設定
	D3D12_RESOURCE_DESC resourceDesc{};
	resourceDesc.Width = width;                                   // Textureの横幅
	resourceDesc.Height = height;                                 // Textureの縦幅
	resourceDesc.MipLevels = 1;                                   // mipMapの数
	resourceDesc.DepthOrArraySize = 1;                            // 奥行or配列Textureの配列数
	resourceDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;          // DepthStencilとして利用可能なフォーマット
	resourceDesc.SampleDesc.Count = 1;                            // サンプリングカウント。1固定
	resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;  // 2次元
	resourceDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL; // DepthStencilとして使う通知

	// 利用するHeapの設定
	D3D12_HEAP_PROPERTIES heapPropertis{};
	heapPropertis.Type = D3D12_HEAP_TYPE_DEFAULT; // VRAM上に作成

	// 深度のクリア設定
	D3D12_CLEAR_VALUE depthClearValue{};
	depthClearValue.DepthStencil.Depth = 1.0f;              // 1.0f(最大値)でクリア
	depthClearValue.Format = DXGI_FORMAT_D24_UNORM_S8_UINT; // ResourceDescと同じにする

	// Resourceの生成
	Microsoft::WRL::ComPtr<ID3D12Resource> resource = nullptr;
	HRESULT hr = device->CreateCommittedResource(
		&heapPropertis,                   // Heapの設定
		D3D12_HEAP_FLAG_NONE,             // Heapの特殊な設定。無し
		&resourceDesc,                    // Resourceの設定
		D3D12_RESOURCE_STATE_DEPTH_WRITE, // 鮮度を書き込む状態にする
		&depthClearValue,                 // Clear最適値
		IID_PPV_ARGS(&resource));         // 作成するResourceポインタへのポインタ

	assert(SUCCEEDED(hr));

	return resource;
}

SoundData SoundLoadWave(const char* filename)
{
	//HRESULT result;

	//===========================
	// ファイルを開く
	//===========================
	// fstreamのインスタンス
	std::ifstream file;

	// .wavファイルをバイナリモードで開く
	file.open(filename, std::ios_base::binary);

	// ファイルが開けなかったら停止
	assert(file.is_open());

	//===========================
	// .wavデータを読み込み
	//===========================
	// RIFFヘッダの読み込み
	RiffHeader riff;
	file.read((char*)&riff, sizeof(riff));

	// ファイルがRIFFかチェック
	if (strncmp(riff.chunk.id, "RIFF", 4) != 0) {
		assert(0);
	}

	// RIFFならファイルタイプがWAVEかチェック
	// MP3を拡張子だけ.wavにしてもダメ。やりたいなら変換して
	if (strncmp(riff.type, "WAVE", 4) != 0) {
		assert(0);
	}

	// 一時的にチャンクヘッダ（IDとサイズ）を格納する変数を用意
	ChunkHeader chunk = {};

	// ファイルからチャンクヘッダ（4バイトのIDと4バイトのサイズ）を読み込む
	file.read((char*)&chunk, sizeof(ChunkHeader));

	// チャンクIDが "fmt " であることを確認（フォーマットチャンクでなければ停止）
	if (strncmp(chunk.id, "fmt ", 4) != 0) {
		assert(0);  // フォーマットチャンク以外だった場合は処理を中断
	}

	// フォーマットチャンク用の構造体を初期化
	FormatChunk format = {};

	// 読み込んだチャンクヘッダ情報を FormatChunk にコピー
	format.chunk = chunk;

	// ファイルからチャンク本体（波形フォーマット情報）を読み込む(40Byte対応Ver)
	// サイズはチャンクヘッダに記録されていたサイズに基づいて読み込む
	const size_t kFmtLimitSize = sizeof(format.fmt);             // 通常18バイト
	std::vector<char> buffer(chunk.size);                        // チャンクサイズ分の一時バッファを確保
	file.read(buffer.data(), chunk.size);                        // チャンクサイズ分を読み込む
	std::memcpy(&format.fmt, buffer.data(), kFmtLimitSize);      // 先頭kFmtLimitSize分だけをコピー

	// Dataチャンクの読み込み
	ChunkHeader data;
	file.read((char*)&data, sizeof(data));

	// JUNKチャンクを検知した場合の処理
	if (strncmp(data.id, "JUNK", 4) == 0) {
		// 読み取り位置をJUNKチャンクの終わりまで進める
		file.seekg(data.size, std::ios_base::cur);

		// 再読み込み
		file.read((char*)&data, sizeof(data));
	}

	if (strncmp(data.id, "data", 4) != 0) {
		assert(0);
	}

	// Dataチャンクのデータ部(波形データ)の読み込み
	// Dataチャンクは音声ファイルごとに異なるのでバッファを動的確保
	char* pBuffer = new char[data.size];
	file.read(pBuffer, data.size);

	//===========================
	// ファイルを閉じる
	//===========================
	// WAVEファイルを閉じる
	file.close();

	//===========================
	// SoundDataを返す
	//===========================
	SoundData soundData = {};

	soundData.wfex = format.fmt; // 波形フォーマット
	soundData.pBuffer = reinterpret_cast<BYTE*>(pBuffer); // 波形データ
	soundData.bufferSize = data.size; // 波形データのサイズ

	return soundData;
}

void SoundUnload(SoundData* soundData)
{
	// バッファのメモリを解放
	delete[] soundData->pBuffer;

	soundData->pBuffer = 0;
	soundData->bufferSize = 0;
	soundData->wfex = {};
}

void SoundPlayWave(IXAudio2* xAudio2, const SoundData& soundData)
{
	HRESULT result;

	// 波形フォーマットを基にSourceVoiceの生成
	IXAudio2SourceVoice* pSourceVoice = nullptr;
	result = xAudio2->CreateSourceVoice(&pSourceVoice, &soundData.wfex);
	assert(SUCCEEDED(result));

	// 再生する波形データの設定
	XAUDIO2_BUFFER buf{};
	buf.pAudioData = soundData.pBuffer;
	buf.AudioBytes = soundData.bufferSize;
	buf.Flags = XAUDIO2_END_OF_STREAM;

	// 波形データの再生
	result = pSourceVoice->SubmitSourceBuffer(&buf);
	result = pSourceVoice->Start();
}

ModelData LoadObjFile(const std::string& directorPath, const std::string& filename)
{
	//==========================
	// 必要なファイルの宣言
	//==========================
	ModelData modelData; // 構築するModelData
	std::vector<Vector4> positions; // 位置
	std::vector<Vector3> normals; // 法線
	std::vector<Vector2> texcoords; // テクスチャの座標
	std::string line; // ファイルから読んだ1行を格納する場所

	//==========================
	// ファイルを開く
	//==========================
	std::ifstream file(directorPath + "/" + filename); // ファイルを開く
	assert(file.is_open()); // 開けなかったら止める

	//==========================================
	// 実際にファイルを読み、ModelDataを構築していく
	//==========================================
	while (std::getline(file, line)) {
		std::string identifier;
		std::istringstream s(line);
		s >> identifier; // 先頭の識別子を読む

		//=============================
		// identifierに応じて処理をしていく
		//=============================
		if (identifier == "v") { // "v" :頂点位置
			Vector4 position;
			s >> position.x >> position.y >> position.z;
			position.w = 1.0f;
			positions.push_back(position);

		} else if (identifier == "vt") { // "vt":頂点テクスチャ座標
			Vector2 texcood;
			s >> texcood.x >> texcood.y;
			texcoords.push_back(texcood);

		} else if (identifier == "vn") { // "vn":頂点法線
			Vector3 normal;
			s >> normal.x >> normal.y >> normal.z;
			normals.push_back(normal);

		} else if (identifier == "f") { // "f" :面
			VertexData triangle[3];

			// 面は三角形限定。その他は未対応
			for (int32_t faceVertex = 0; faceVertex < 3; ++faceVertex) {
				std::string vertexDefinition;
				s >> vertexDefinition;
				// 頂点の要素へのIndexは「位置/UV/法線」で格納されているので、分解してIndexを取得する
				std::istringstream v(vertexDefinition);
				uint32_t elementIndices[3];
				for (int32_t element = 0; element < 3; ++element) {
					std::string index;
					std::getline(v, index, '/');// "/"区切りでindexを読んでいく
					elementIndices[element] = std::stoi(index);
				}

				// 要素へのIndexから、実際の要素の値を取得して頂点を構築する
				// 1始まりだから-1しておく
				Vector4 position = positions[elementIndices[0] - 1];
				Vector2 texcoord = texcoords[elementIndices[1] - 1];
				Vector3 normal = normals[elementIndices[2] - 1];
				texcoord.y = 1.0f - texcoord.y;

				position.x *= -1.0f;
				normal.x *= -1.0f;

				triangle[faceVertex] = { position,texcoord,normal };

			}

			// 頂点を逆順で登録することで回り順を逆にする
			modelData.vertices.push_back(triangle[2]);
			modelData.vertices.push_back(triangle[1]);
			modelData.vertices.push_back(triangle[0]);

		} else if (identifier == "mtllib") {
			// materialTemplateLibraryファイルの名前を取得する
			std::string materialFilename;
			s >> materialFilename;

			// 基本的にobjファイルと同一階層にmtlは存在するので、ディレクトリ名とファイル名を渡す
			modelData.material = LoadMaterialTemplateFile(directorPath, materialFilename);

		}

	}

	//==========================
	// ModelDataを返す
	//==========================
	return modelData;

}

MaterialData LoadMaterialTemplateFile(const std::string& directoryPath, const std::string& filename)
{
	//======================
	// 中で必要となる変数の宣言
	//======================
	MaterialData materialData; // 構築するMaterialData
	std::string line; // ファイルから読んだ1行を格納する場所

	//======================
	// ファイルを開く
	//======================
	std::ifstream file(directoryPath + "/" + filename); // ファイルを開く
	assert(file.is_open()); // 開けなかったら止める

	//============================================
	// 実際にファイルを読み,MaterialDataを構築していく
	//============================================
	while (std::getline(file, line)) {
		std::string identifier;
		std::istringstream s(line);
		s >> identifier;

		//=============================
		// identifierに応じて処理をしていく
		//=============================
		if (identifier == "map_Kd") {
			std::string textureFilename;
			s >> textureFilename;

			// 連結してファイルパスにする
			materialData.textureFilePath = directoryPath + "/" + textureFilename;

		}

	}

	//======================
	// MaterialDataを返す
	//======================
	return materialData;

}

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

D3D12_CPU_DESCRIPTOR_HANDLE GetCPUDescriptorHandle(Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> descriptorHeap, uint32_t descriptorSize, uint32_t index)
{
	D3D12_CPU_DESCRIPTOR_HANDLE handleCPU;

	handleCPU = descriptorHeap->GetCPUDescriptorHandleForHeapStart();
	handleCPU.ptr += (descriptorSize * index);

	return handleCPU;
}

D3D12_GPU_DESCRIPTOR_HANDLE GetGPUDescriptorHandle(Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> descriptorHeap, uint32_t descriptorSize, uint32_t index)
{
	D3D12_GPU_DESCRIPTOR_HANDLE handleGPU;

	handleGPU = descriptorHeap->GetGPUDescriptorHandleForHeapStart();
	handleGPU.ptr += (descriptorSize * index);

	return handleGPU;
}