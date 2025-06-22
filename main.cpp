#include <Windows.h>
#include <cstdint>
#include <format>
#include <string>
#include <math.h>
#define _USE_MATH_DEFINES
#include <vector>

// DirectX12
#include <d3d12.h>
#include <dxgi1_6.h>
#include <cassert>
#include <dxgidebug.h>
#include <iostream>
#include <dxcapi.h>
#include "externals/DirectXTex/DirectXTex.h"
#include "externals/DirectXTex/d3dx12.h"

#pragma comment(lib,"d3d12.lib")
#pragma comment(lib,"dxgi.lib")
#pragma comment(lib,"dxguid.lib")
#pragma comment(lib,"dxcompiler.lib")

//ImGUI
#include "externals/ImGUI/imgui.h"
#include "externals/ImGUI/imgui_impl_dx12.h"
#include "externals/ImGUI/imgui_impl_win32.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

// 自作ヘッダーのインクルード
#include "ConvertStringClass.h"
#include "Vector3.h"
#include "Vector4.h"
#include "Transform.h"
#include "VertexData.h"
#include "Matrix4x4.h"
#include "Material.h"
#include "TransformationMatrix.h"
#include "DirectionalLight.h"

//============================
// 関数の宣言
//============================

// string->wstring
std::wstring ConvertString(const std::string& str);

// wstring->string
std::string ConvertString(const std::wstring& str);

// 出力ウィンドウにログを出す関数
void Log(const std::string& message) {
	OutputDebugStringA(message.c_str());
}


/// <summary>
/// CompileShader関数
/// </summary>
/// <param name="filePath">CompilerするShaderファイルへのパス</param>
/// <param name="profile">Compilerに使用するProfile</param>
/// <param name="dxcUtils">初期化で生成したdxcUtils</param>
/// <param name="dxcCompiler">初期化で生成したdxcCompiler</param>
/// <param name="includeHandler">初期化で生成したincludeHandler</param>
/// <returns></returns>
IDxcBlob* CompileShader(
	const std::wstring& filePath,
	const wchar_t* profile,
	IDxcUtils* dxcUtils,
	IDxcCompiler3* dxcCompiler,
	IDxcIncludeHandler* includeHandler);

ID3D12Resource* CreateBufferResource(ID3D12Device* device, size_t sizeInBytes);

ID3D12DescriptorHeap* CreateDescriptorHeap(
	ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE heapType, UINT numDescriptor, bool shaderVicible);

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
ID3D12Resource* CreateTextureResource(ID3D12Device* device, const DirectX::TexMetadata& metadata);

/// <summary>
/// TextureResourceにデータを転送する関数
/// </summary>
/// <param name="texture"></param>
/// <param name="mipImages"></param>
void UploadTextureData(ID3D12Resource* texture, const DirectX::ScratchImage& mipImages);

[[nodiscard]]
ID3D12Resource* UploadTextureData(ID3D12Resource* texture, const DirectX::ScratchImage& mipImages, ID3D12Device* device, ID3D12GraphicsCommandList* commandList);

ID3D12Resource* CreateDepthStencilTextureResource(ID3D12Device* device, int32_t width, int32_t height);

//===================================
// MT3でも使う関数の宣言
//===================================

/// <summary>
/// cotangent(余接)を求める関数
/// </summary>
/// <param name="theta">θ(シータ)</param>
/// <returns>cotangent</returns>
float Cotangent(float theta);

/// <summary>
/// 4x4行列の積を求める関数
/// </summary>
/// <param name="matrix1">1つ目の行列</param>
/// <param name="matrix2">1つ目の行列</param>
/// <returns>4x4行列の積</returns>
Matrix4x4 Multiply(Matrix4x4 matrix1, Matrix4x4 matrix2);

/// <summary>
/// 4x4単位行列作成関数
/// </summary>
/// <returns>4x4単位行列</returns>
Matrix4x4 MakeIdentity4x4();

/// <summary>
/// 座標系変換関数
/// </summary>
/// <param name="vector3"></param>
/// <param name="matrix4x4"></param>
/// <returns>デカルト座標系</returns>
Vector3 TransformCoordinate(const Vector3& vector3, const Matrix4x4& matrix4x4);

/// <summary>
/// アフィン行列作成関数
/// </summary>
/// <param name="scale">縮尺</param>
/// <param name="rotate">thetaを求めるための数値</param>
/// <param name="translate">三次元座標でのx,y,zの移動量</param>
/// <returns>アフィン行列</returns>
Matrix4x4 MakeAffineMatrix(Transform& transform);

/// <summary>
/// 投視投影行列作成関数
/// </summary>
/// <param name="fovY">縦の画角</param>
/// <param name="aspectRatio">アスペクト比</param>
/// <param name="nearClip">近平面への距離</param>
/// <param name="farClip">遠平面への距離</param>
/// <returns>投視投影行列</returns>
Matrix4x4 MakePerspectiveFovMatrix(float fovY, float aspectRatio, float nearClip, float farClip);

/// <summary>
/// 正射影行列作関数
/// </summary>
/// <param name="left">左側の座標</param>
/// <param name="top">上側の座標</param>
/// <param name="right">右側の座標</param>
/// <param name="bottom">下側の座標</param>
/// <param name="nearClip">近平面への距離</param>
/// <param name="farClip">遠平面への距離</param>
/// <returns>正射影行列</returns>
Matrix4x4 MakeOrthographicMatrix(float left, float top, float right, float bottom, float nearClip, float farClip);

/// <summary>
/// 4x4逆行列を求める関数
/// </summary>
/// <param name="matrix4x4">逆行列を求めたい行列</param>
/// <returns>4x4逆行列</returns>
Matrix4x4 Inverse(Matrix4x4 matrix4x4);

void DrawSphere(const Vector3& center, float radius, const Matrix4x4& viewProjectionMatrix, const Matrix4x4& viewportMatrix, uint32_t color);

Vector3 TransformMatrix(const Vector3& vector, const Matrix4x4& matrix);

D3D12_CPU_DESCRIPTOR_HANDLE GetCPUDescriptorHandle(ID3D12DescriptorHeap* descriptorHeap, uint32_t descriptorSize, uint32_t index);

D3D12_GPU_DESCRIPTOR_HANDLE GetGPUDescriptorHandle(ID3D12DescriptorHeap* descriptorHeap, uint32_t descriptorSize, uint32_t index);

LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {

	if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wparam, lparam)) {
		return true;
	}

	// メッセージに応じてゲーム固有の処理を行う
	switch (msg) {
		// ウィンドウが破棄された

	case WM_DESTROY:
		// OSに対して、アプリの終了を伝える
		PostQuitMessage(0);
		return 0;

	}

	// 標準のメッセージ処理を行う
	return DefWindowProc(hwnd, msg, wparam, lparam);
}

// Windowsアプリでのエントリーポイント(main関数)
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {

	// COMの初期化
	//CoInitializeEx(0, COINIT_MULTITHREADED);

	//--------ここ資料と違うけど警告出るから変更してる------------------------------------
	HRESULT hr = CoInitializeEx(0, COINIT_MULTITHREADED);
	assert(SUCCEEDED(hr));

	// クラスの生成
	ConvertStringClass* convertStringClass = new ConvertStringClass;

	//====================
	// ウィンドウクラスの登録
	//====================
	WNDCLASS wc{};

	// ウィンドウプロシージャ
	wc.lpfnWndProc = WindowProc;

	// ウィンドウクラス名
	wc.lpszClassName = L"CG2WindowClass";

	// インスタンスハンドル
	wc.hInstance = GetModuleHandle(nullptr);

	// カーソル
	wc.hCursor = LoadCursor(nullptr, IDC_ARROW);

	// ウィンドウクラスを登録する
	RegisterClass(&wc);

	//========================
	// ウィンドウサイズを決める
	//========================
	// クライアント領域のサイズ
	const int32_t kClientWidth = 1280;
	const int32_t kClientHeight = 720;

	// ウィンドウサイズを表す構造体にクライアント領域を入れる
	RECT wrc = { 0,0,kClientWidth,kClientHeight };

	// クライアント領域を元に実際のサイズにwrcを変更してもらう
	AdjustWindowRect(&wrc, WS_OVERLAPPEDWINDOW, false);

	//================
	// ウィンドウの生成
	//================
	HWND hwnd = CreateWindow(
		wc.lpszClassName,     // 利用するクラス名
		L"CG2_0_3",           // タイトルバーの文字
		WS_OVERLAPPEDWINDOW,  // よく見るウィンドウスタイル
		CW_USEDEFAULT,        // 表示X座標(Windowsに任せる)
		CW_USEDEFAULT,        // 表示Y座標(Windowsに任せる)
		wrc.right - wrc.left, // ウィンドウの横幅
		wrc.bottom - wrc.top, // ウィンドウの縦幅
		nullptr,              // 親ウィンドウハンドル
		nullptr,              // メニューハンドル
		wc.hInstance,         // インスタンスハンドル
		nullptr);             // オプション

	//========================================
	// ここにデバッグレイヤー デバックの時だけ出る
	//========================================
#ifdef _DEBUG
	ID3D12Debug1* debugController = nullptr;
	if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) {
		// デバッグレイヤーを有効化する
		debugController->EnableDebugLayer();

		// GPU側でもチェックを行うようにする
		debugController->SetEnableGPUBasedValidation(TRUE);
	}

#endif // DEBUG

	// ウィンドウを表示する
	ShowWindow(hwnd, SW_SHOW);

	//==========================
	// DXGIファクトリーの生成
	//==========================
	IDXGIFactory7* dxgiFactory = nullptr;

	// HRESULTはWindows系のエラーコードであり、関数が成功したかどうかをSUCCEEDECマクロで判断できる
	hr = CreateDXGIFactory(IID_PPV_ARGS(&dxgiFactory));

	// 初期化の根本的な部分でエラーが出た場合はプログラムが間違っているかどうにもできない場合が多いのでassertにしておく
	assert(SUCCEEDED(hr));

	//==========================
	// アダプタ ~~ GPU
	//==========================
	// 使用するアダプタ用の変数
	IDXGIAdapter4* useAdapter = nullptr;

	// 良い順にアダプタを頼む
	for (UINT i = 0; dxgiFactory->EnumAdapterByGpuPreference(i,
		DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&useAdapter)) !=
		DXGI_ERROR_NOT_FOUND; ++i) {

		// アダプタの情報を取得
		DXGI_ADAPTER_DESC3 adapterDesc{};
		hr = useAdapter->GetDesc3(&adapterDesc);
		assert(SUCCEEDED(hr));// 取得できないのは一大事

		// ソフトウェアアダプタ出なければ採用!
		if (!(adapterDesc.Flags & DXGI_ADAPTER_FLAG3_SOFTWARE)) {

			// 採用したアダプタの情報をログに出力<-wstring
			Log(convertStringClass->ConvertString(std::format(L"Use Adapter:{}\n", adapterDesc.Description)));

			break;
		}

		// ソフトウェアアダプタの場合は見なかったことにする
		useAdapter = nullptr;
	}

	// 適切なアダプタが見つから無かったので起動できない
	assert(useAdapter != nullptr);

	ID3D12Device* device = nullptr;

	// 機能レベルとログ出力用の文字列
	D3D_FEATURE_LEVEL featureLevels[] = {
		D3D_FEATURE_LEVEL_12_2,D3D_FEATURE_LEVEL_12_1,D3D_FEATURE_LEVEL_12_0
	};

	const char* kFeatureLevelString[] = { "12.2","12.1","12.0" };

	// 高い順に生成できるか試していく
	for (size_t i = 0; i < _countof(featureLevels); ++i) {

		// 採用したアダプタでデバイスを生成
		hr = D3D12CreateDevice(useAdapter, featureLevels[i], IID_PPV_ARGS(&device));

		// 指定した機能レベルでデバイスを生成できたか確認
		if (SUCCEEDED(hr)) {

			// 生成できたのでログを出力してループを抜ける
			Log(std::format("FutureLevel:{}\n", kFeatureLevelString[i]));

			break;
		}
	}

	// デバイスの生成が上手くいかなかったので起動できない
	assert(device != nullptr);

	// 初期化完了のログを出力
	Log("Complete create D3D12Device!!\n");

	//=========================
	// エラー、警告が出たら即停止
	//=========================
#ifdef _DEBUG
	ID3D12InfoQueue* infoQueue = nullptr;
	if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&infoQueue)))) {
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

		// 解放
		infoQueue->Release();

	}

#endif // _DEBUG


	//====================
	// コマンドキューを生成
	//====================
	ID3D12CommandQueue* commandQueue = nullptr;
	D3D12_COMMAND_QUEUE_DESC commandQueueDesc{};
	hr = device->CreateCommandQueue(&commandQueueDesc, IID_PPV_ARGS(&commandQueue));

	// コマンドキューの生成が上手くできなかったので起動できない
	assert(SUCCEEDED(hr));

	// コマンドアロケータを生成する
	ID3D12CommandAllocator* commandAllocator = nullptr;
	hr = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&commandAllocator));

	// コマンドアロケータの生成が上手くできなかったので起動できない
	assert(SUCCEEDED(hr));

	//=========================
	// コマンドリストを生成する
	//=========================
	ID3D12GraphicsCommandList* commandList = nullptr;
	hr = device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
		commandAllocator, nullptr, IID_PPV_ARGS(&commandList));

	// コマンドリストの生成が上手くできなかったので起動できない
	assert(SUCCEEDED(hr));

	//===========================
	// 画面の色を変えよう
	//===========================
	// スワップチェーンを生成する
	assert(hwnd != nullptr); // ウィンドウハンドルがnullでないことを確認

	IDXGISwapChain4* swapChain = nullptr;
	DXGI_SWAP_CHAIN_DESC1 swapChainDesc{};
	swapChainDesc.Width = kClientWidth;                          // 画面の幅
	swapChainDesc.Height = kClientHeight;                        // 画面の高さ
	swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;           // 色の形式
	swapChainDesc.SampleDesc.Count = 1;                          // マルチサンプルしない
	swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; // 描画のターゲットとして利用する
	swapChainDesc.BufferCount = 2;                               // ダブルバッファ
	swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;    // モニタに映したら中身を破棄

	// コマンドキュー、ウィンドウハンドル、設定を譲渡して生成
	hr = dxgiFactory->CreateSwapChainForHwnd(commandQueue, hwnd, &swapChainDesc, nullptr, nullptr,
		reinterpret_cast<IDXGISwapChain1**>(&swapChain));

	// 生成が上手くできなかったので起動できない
	assert(SUCCEEDED(hr));

	// Textureを2枚読み込んで転送
	DirectX::ScratchImage mipImages[2] = {
		LoadTexture("Resources/images/uvChecker.png"),
		LoadTexture("Resources/images/monsterBall.png") };

	const DirectX::TexMetadata& metadata = mipImages[0].GetMetadata();
	const DirectX::TexMetadata& metadata2 = mipImages[1].GetMetadata();

	ID3D12Resource* textureResource[2] = {
		CreateTextureResource(device, metadata),
		CreateTextureResource(device, metadata2) };

	ID3D12Resource* intermediateResource[2] = {
		UploadTextureData(textureResource[0], mipImages[0], device, commandList),
		UploadTextureData(textureResource[1], mipImages[1], device, commandList) };


	// DepthStencilTextureをウィンドウサイズで作成
	ID3D12Resource* depthStencilResource = CreateDepthStencilTextureResource(device, kClientWidth, kClientHeight);

	//=========================
	// DescriptorHeapを生成する
	//=========================
	// RTV用のヒープでディスクリプタの数は2。RTVはShader内で触るものではないので、ShaderVisibleはfalse
	ID3D12DescriptorHeap* rtvDescriptorHeap = CreateDescriptorHeap(device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 2, false);

	// SRV用のヒープでディスクリプタの数は128。SRVはShader内で使うのでShaderVisibleはtrue
	ID3D12DescriptorHeap* srvDescriptorHeap = CreateDescriptorHeap(device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 128, true);

	// DSV用のヒープでディスクリプタの数は1。DSVはShader内で触るものではないので、ShaderVisibleはfalse
	ID3D12DescriptorHeap* dsvDescriptorHeap = CreateDescriptorHeap(device, D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1, false);

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
	// Depthの昨日を有効化する
	depthStencilDesc.DepthEnable = true;
	// 書き込む
	depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
	// 比較関数はLessEqual。つまり、近ければ描画される。変更したい場合は3_1の22ページを参照
	depthStencilDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;

	const uint32_t descriptorSizeSRV = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	const uint32_t descriptorSizeRTV = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	const uint32_t descriptorSizeDSV = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);

	// SRVを作成するDescriptorHeapの場所を決める
	D3D12_CPU_DESCRIPTOR_HANDLE textureSrvHandleCPU[2] = {
		GetCPUDescriptorHandle(srvDescriptorHeap,descriptorSizeSRV,1),
		GetCPUDescriptorHandle(srvDescriptorHeap,descriptorSizeSRV,2)
	};

	D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandleGPU[2] = {
		GetGPUDescriptorHandle(srvDescriptorHeap,descriptorSizeSRV,1),
		GetGPUDescriptorHandle(srvDescriptorHeap,descriptorSizeSRV,2)
	};

	// 先頭はImGUIが使用するのでその次から使う
	// 1枚目
	textureSrvHandleCPU[0].ptr += device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	textureSrvHandleGPU[0].ptr += device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	// 2枚目
	textureSrvHandleCPU[1].ptr += device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV) * 2;
	textureSrvHandleGPU[1].ptr += device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV) * 2;

	// SRVの生成
	device->CreateShaderResourceView(textureResource[0], &srvDesc[0], textureSrvHandleCPU[0]);
	device->CreateShaderResourceView(textureResource[1], &srvDesc[1], textureSrvHandleCPU[1]);

	// DSVHeapの先頭にDSVを作る
	device->CreateDepthStencilView(depthStencilResource, &dsvDesc, dsvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());

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
	ID3D12Resource* swapChainResources[2] = { nullptr };
	hr = swapChain->GetBuffer(0, IID_PPV_ARGS(&swapChainResources[0]));

	//上手く取得できなかったので起動できない
	assert(SUCCEEDED(hr));

	// 1もやる
	hr = swapChain->GetBuffer(1, IID_PPV_ARGS(&swapChainResources[1]));

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
	device->CreateRenderTargetView(swapChainResources[0], &rtvDesc, rtvHandles[0]);

	// 2つ目のディスクリプタハンドルを取得
	rtvHandles[1].ptr = rtvHandles[0].ptr + device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

	// 2つ目を作る
	device->CreateRenderTargetView(swapChainResources[1], &rtvDesc, rtvHandles[1]);

	//==========================
	// FenceとEventを作る
	//==========================
	// 初期値0でFenceを作る
	ID3D12Fence* fence = nullptr;
	uint64_t fenceValue = 0;
	hr = device->CreateFence(fenceValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence));
	assert(SUCCEEDED(hr));

	// FenceのSignalを待つためのイベントを作成
	HANDLE fenceEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
	assert(fenceEvent != nullptr);

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

	ID3D12Resource* directionalLightResource = CreateBufferResource(device, sizeof(DirectionalLight));
	DirectionalLight* directionalLightData = nullptr;
	directionalLightResource->Map(0, nullptr, reinterpret_cast<void**>(&directionalLightData));
	directionalLightData->color = { 1.0f,1.0f,1.0f,1.0f };
	directionalLightData->direction = { 0.0f,-1.0f,0.0f };
	directionalLightData->intensity = 1.0f;

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
	ID3DBlob* signatureBlob = nullptr;
	ID3DBlob* errorBlob = nullptr;
	hr = D3D12SerializeRootSignature(&descriptionRootSignature,
		D3D_ROOT_SIGNATURE_VERSION_1, &signatureBlob, &errorBlob);
	if (FAILED(hr)) {
		Log(reinterpret_cast<char*>(errorBlob->GetBufferSize()));
		assert(false);
	}

	// バイナリをもとに生成
	ID3D12RootSignature* rootSignature = nullptr;
	hr = device->CreateRootSignature(0,
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

	// 全ての色要素を書き込む
	blendDesc.RenderTarget[0].RenderTargetWriteMask =
		D3D12_COLOR_WRITE_ENABLE_ALL;

	// ResiterzerStateの設定
	D3D12_RASTERIZER_DESC rasterizerDesc{};

	// 裏面(時計回り)を表示しない
	rasterizerDesc.CullMode = D3D12_CULL_MODE_BACK;

	// 三角形の中を塗りつぶす
	rasterizerDesc.FillMode = D3D12_FILL_MODE_SOLID;

	// シェーダーをコンパイルする
	IDxcBlob* vertexShaderBlob = CompileShader(L"Object3d.VS.hlsl",
		L"vs_6_0", dxcUtils, dxcCompiler, includeHandler);
	assert(vertexShaderBlob != nullptr);

	IDxcBlob* pixelShaderBlob = CompileShader(L"Object3d.PS.hlsl",
		L"ps_6_0", dxcUtils, dxcCompiler, includeHandler);
	assert(vertexShaderBlob != nullptr);

	//===============
	//PSOを生成
	//===============
	D3D12_GRAPHICS_PIPELINE_STATE_DESC graphicsPipelineStateDesc{};
	graphicsPipelineStateDesc.pRootSignature = rootSignature;
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
	ID3D12PipelineState* graphicsPipelineState = nullptr;
	hr = device->CreateGraphicsPipelineState(&graphicsPipelineStateDesc,
		IID_PPV_ARGS(&graphicsPipelineState));
	assert(SUCCEEDED(hr));

	//=========================
	// VertexResourceを生成
	//=========================
	// 球を配置するため*1024(頂点の数)にする
	ID3D12Resource* vertexResource = CreateBufferResource(device, sizeof(VertexData) * 1024);

	// 球のindexResource
	ID3D12Resource* indexResourceSphre = CreateBufferResource(device, sizeof(uint32_t) * 1536);

	// sprite用の頂点リソースを作る
	ID3D12Resource* vertexResourceSprite = CreateBufferResource(device, sizeof(VertexData) * 4);

	// sprite用のIndexリソースを作る
	ID3D12Resource* indexResourceSprite = CreateBufferResource(device, sizeof(uint32_t) * 6);

	//=============================
	// Material用のResourceの作成
	//=============================
	// マテリアル用のリソースを作る。今回はMaterial1つ分のサイズを用意
	ID3D12Resource* materialResource = CreateBufferResource(device, sizeof(Material));

	// スプライト用のマテリアルリソース
	ID3D12Resource* materialResourceSprite = CreateBufferResource(device, sizeof(Material));

	// マテリアルにデータを書き込む
	Material* materialData = nullptr;
	Material* materialDataSprite = nullptr;

	// 書き込むためのアドレスを取得
	materialResource->Map(0, nullptr, reinterpret_cast<void**>(&materialData));
	materialResourceSprite->Map(0, nullptr, reinterpret_cast<void**>(&materialDataSprite));

	// 白で書き込む
	materialData->color = Vector4(1.0f, 1.0f, 1.0f, 1.0f);
	materialDataSprite->color = Vector4(1.0f, 1.0f, 1.0f, 1.0f);

	// 球のみLightingを有効にする
	materialData->enableLighting = true;
	materialDataSprite->enableLighting = false;

	// Sprite用のTransformationMatrix用のリソースを作る。Matrix4x4 1つ分のサイズを用意する
	ID3D12Resource* transformationMatrixResourceSprite = CreateBufferResource(device, sizeof(Matrix4x4) * 2);

	// データを書き込む
	Matrix4x4* transformationMatrixDataSprite = nullptr;

	// 書き込むためのアドレスを取得
	transformationMatrixResourceSprite->Map(0, nullptr, reinterpret_cast<void**>(&transformationMatrixDataSprite));

	// 単位行列を書き込んでおく
	*transformationMatrixDataSprite = MakeIdentity4x4();

	//============================================
	// TransformationMatrix用のResourceの作成
	//============================================
	UINT transformationMatrixSize = (sizeof(TransformationMatrix) + 255) & ~255;

	// WVP用のリソースを作る。
	ID3D12Resource* wvpResource = CreateBufferResource(device, transformationMatrixSize);

	// データを書き込む
	TransformationMatrix* wvpDate = nullptr;

	// 書き込むためのアドレスを取得
	wvpResource->Map(0, nullptr, reinterpret_cast<void**>(&wvpDate));

	// 単位行列を書き込む
	wvpDate->WVP = MakeIdentity4x4();
	wvpDate->World = MakeIdentity4x4();

	//===================================
	// BufferViewの作成
	//===================================
	// 頂点バッファビューを作成
	D3D12_VERTEX_BUFFER_VIEW vertexBufferView{};

	// リソースの先頭アドレスから使用
	vertexBufferView.BufferLocation = vertexResource->GetGPUVirtualAddress();

	// 使用するリソースのサイズは頂点1024個のサイズ
	vertexBufferView.SizeInBytes = sizeof(VertexData) * 1024;

	// 1頂点当たりのサイズ
	vertexBufferView.StrideInBytes = sizeof(VertexData);

	// sprite用の頂点バッファビューを作成
	D3D12_VERTEX_BUFFER_VIEW vertexBufferViewSprite{};

	// Spere用のindexバッファビューを作る
	D3D12_INDEX_BUFFER_VIEW indexBufferViewSphere{};
	indexBufferViewSphere.BufferLocation = indexResourceSphre->GetGPUVirtualAddress();
	indexBufferViewSphere.SizeInBytes = sizeof(uint32_t) * 1536;
	indexBufferViewSphere.Format = DXGI_FORMAT_R32_UINT;

	// リソースの先頭アドレスから使用
	vertexBufferViewSprite.BufferLocation = vertexResourceSprite->GetGPUVirtualAddress();

	// 使用するリソースのサイズは頂点4個のサイズ
	vertexBufferViewSprite.SizeInBytes = sizeof(VertexData) * 4;

	// 1頂点あたりのサイズ
	vertexBufferViewSprite.StrideInBytes = sizeof(VertexData);

	// sprite用のindexバッファビューを作る
	D3D12_INDEX_BUFFER_VIEW indexBufferViewSprite{};

	// リソースの先頭アドレスから使う
	indexBufferViewSprite.BufferLocation = indexResourceSprite->GetGPUVirtualAddress();

	// 使用するリソースのサイズはインデックスの6つ分のサイズ
	indexBufferViewSprite.SizeInBytes = sizeof(uint32_t) * 6;

	// インデックスはuint32_tとする
	indexBufferViewSprite.Format = DXGI_FORMAT_R32_UINT;

	//============================
	// Resourceにデータを書き込む
	//============================
	// 頂点リソースにデータを書き込む
	VertexData* vertexData = nullptr;

	// 書き込むためのアドレスを取得
	vertexResource->Map(0, nullptr, reinterpret_cast<void**>(&vertexData));

	// インデックスリソースにデータを書き込む
	uint32_t* indexDataSphere = nullptr;
	indexResourceSprite->Map(0, nullptr, reinterpret_cast<void**>(&indexDataSphere));

	const float kSubdivision = 16.0f;

	// 経度分割1つ分の角度φd
	const float kLonEvery = static_cast<float>(M_PI) * 2.0f / kSubdivision;

	// 緯度分割1つ分の角度θd
	const float kLatEvery = static_cast<float>(M_PI) / kSubdivision;

	// 緯度の方向に分割
	for (int latIndex = 0; latIndex < kSubdivision; ++latIndex) {
		float lat = static_cast<float>(-M_PI) / 2.0f + kLatEvery * latIndex;// θ

		// 経度の方向に分割しながら線を描く
		for (int lonIndex = 0; lonIndex < kSubdivision; ++lonIndex) {
			uint32_t start = (latIndex * static_cast<int>(kSubdivision) + lonIndex) * 4;
			float lon = lonIndex * kLonEvery;// φ

			// 画像のUVを計算(画像の縦横の場所)
			float u = static_cast<float>(lonIndex) / static_cast<float>(kSubdivision);
			float v = 1.0f - static_cast<float>(latIndex) / static_cast<float>(kSubdivision);

			float u1 = static_cast<float>(lonIndex + 1) / static_cast<float>(kSubdivision);
			float v1 = 1.0f - static_cast<float>(latIndex + 1) / static_cast<float>(kSubdivision);

			// 頂点にデータを入力する。基準点a
			vertexData[start].pos.x = cosf(lat) * cosf(lon);
			vertexData[start].pos.y = sinf(lat);
			vertexData[start].pos.z = cosf(lat) * sinf(lon);
			vertexData[start].pos.w = 1.0f;
			vertexData[start].normal.x = vertexData[start].pos.x;
			vertexData[start].normal.y = vertexData[start].pos.y;
			vertexData[start].normal.z = vertexData[start].pos.z;
			vertexData[start].texcoord = { u,v };

			// b
			vertexData[start + 1].pos.x = cosf(lat + kLatEvery) * cosf(lon);
			vertexData[start + 1].pos.y = sinf(lat + kLatEvery);
			vertexData[start + 1].pos.z = cosf(lat + kLatEvery) * sinf(lon);
			vertexData[start + 1].pos.w = 1.0f;
			vertexData[start + 1].normal.x = vertexData[start + 1].pos.x;
			vertexData[start + 1].normal.y = vertexData[start + 1].pos.y;
			vertexData[start + 1].normal.z = vertexData[start + 1].pos.z;
			vertexData[start + 1].texcoord = { u,v1 };

			// c
			vertexData[start + 2].pos.x = cosf(lat) * cosf(lon + kLonEvery);
			vertexData[start + 2].pos.y = sinf(lat);
			vertexData[start + 2].pos.z = cosf(lat) * sinf(lon + kLonEvery);
			vertexData[start + 2].pos.w = 1.0f;
			vertexData[start + 2].normal.x = vertexData[start + 2].pos.x;
			vertexData[start + 2].normal.y = vertexData[start + 2].pos.y;
			vertexData[start + 2].normal.z = vertexData[start + 2].pos.z;
			vertexData[start + 2].texcoord = { u1,v };

			// d
			vertexData[start + 3].pos.x = cosf(lat + kLatEvery) * cosf(lon + kLonEvery);
			vertexData[start + 3].pos.y = sinf(lat + kLatEvery);
			vertexData[start + 3].pos.z = cosf(lat + kLatEvery) * sinf(lon + kLonEvery);
			vertexData[start + 3].pos.w = 1.0f;
			vertexData[start + 3].normal.x = vertexData[start + 3].pos.x;
			vertexData[start + 3].normal.y = vertexData[start + 3].pos.y;
			vertexData[start + 3].normal.z = vertexData[start + 3].pos.z;
			vertexData[start + 3].texcoord = { u1,v1 };

			// 球体のインデックスリソースにデータを書き込む
			indexDataSphere[start] = start;
			indexDataSphere[start + 1] = start + 1;
			indexDataSphere[start + 2] = start + 2;
			indexDataSphere[start + 3] = start + 1;
			indexDataSphere[start + 4] = start + 3;
			indexDataSphere[start + 5] = start + 2;

		}
	}

	//// 球体のインデックスリソースにデータを書き込む
	//indexDataSphere[0] = 0;
	//indexDataSphere[1] = 1;
	//indexDataSphere[2] = 2;
	//indexDataSphere[3] = 1;
	//indexDataSphere[4] = 3;
	//indexDataSphere[5] = 2;

	// sprite用の頂点データ
	VertexData* vertexDataSprite = nullptr;
	vertexResourceSprite->Map(0, nullptr, reinterpret_cast<void**>(&vertexDataSprite));

	// indexに格納するから同一頂点のデータをわざわざ用意する必要はない
	// 左下
	vertexDataSprite[0].pos = { 0.0f,360.0f,0.0f,1.0f };
	vertexDataSprite[0].texcoord = { 0.0f,1.0f };
	vertexDataSprite[0].normal = { 0.0f,0.0f,-1.0f };

	// 左上
	vertexDataSprite[1].pos = { 0.0f,0.0f,0.0f,1.0f };
	vertexDataSprite[1].texcoord = { 0.0f,0.0f };
	vertexDataSprite[1].normal = { 0.0f,0.0f,-1.0f };

	// 右下
	vertexDataSprite[2].pos = { 640.0f,360.0f,0.0f,1.0f };
	vertexDataSprite[2].texcoord = { 1.0f,1.0f };
	vertexDataSprite[2].normal = { 0.0f,0.0f,-1.0f };

	// 右上
	vertexDataSprite[3].pos = { 640.0f,0.0f,0.0f,1.0f };
	vertexDataSprite[3].texcoord = { 1.0f,0.0f };
	vertexDataSprite[3].normal = { 0.0f,0.0f,-1.0f };

	// インデックスリソースにデータを書き込む
	uint32_t* indexDataSprite = nullptr;
	indexResourceSprite->Map(0, nullptr, reinterpret_cast<void**>(&indexDataSprite));
	indexDataSprite[0] = 0;
	indexDataSprite[1] = 1;
	indexDataSprite[2] = 2;
	indexDataSprite[3] = 1;
	indexDataSprite[4] = 3;
	indexDataSprite[5] = 2;

	//=========================
	// ViewportとScissor
	//=========================
	// ビューポート
	D3D12_VIEWPORT viewport{};

	// クライアント領域のサイズと一緒にして画面全体に表示
	viewport.Width = kClientWidth;
	viewport.Height = kClientHeight;
	viewport.TopLeftX = 0;
	viewport.TopLeftY = 0;
	viewport.MinDepth = 0.0f;
	viewport.MaxDepth = 1.0f;

	// シザー矩形
	D3D12_RECT scissorRect{};

	// 基本的にビューポートと同じ矩形が構成されるようにする
	scissorRect.left = 0;
	scissorRect.right = kClientWidth;
	scissorRect.top = 0;
	scissorRect.bottom = kClientHeight;

	Transform transform{ {1.0f,1.0f,1.0f},{0.0f,0.0f,0.0f},{0.0f,0.0f,0.0f} };
	Transform cameraTransform{ {1.0f,1.0f,1.0f},{0.0f,0.0f,0.0f},{0.0f,0.0f,-10.0f} };
	Transform transformSprite{ {1.0f,1.0f,1.0f},{0.0f,0.0f,0.0f},{0.0f,0.0f,0.0f} };

	//================
	// ImGUIの初期化
	//================
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGui::StyleColorsDark();
	ImGui_ImplWin32_Init(hwnd);
	ImGui_ImplDX12_Init(device,
		swapChainDesc.BufferCount,
		rtvDesc.Format,
		srvDescriptorHeap,
		srvDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
		srvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());

	bool useMonsterBall = true;

	MSG msg{};
	// ウィンドウのxボタンが押されるまでループ
	while (msg.message != WM_QUIT) {
		// Windowsにメッセージが来てたら最優先で処理
		if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		} else {
			// ImGUIにここから始まることを通知
			ImGui_ImplDX12_NewFrame();
			ImGui_ImplWin32_NewFrame();
			ImGui::NewFrame();

			///
			// ここからゲームの処理
			///

			// 開発用UI処理。実際に開発用のuiを出す場合はここをゲーム固有の処理に置き換える.
			// ImGui フレーム開始直後
			ImGui::Begin("Triangle Settings");

			ImGui::SetWindowSize(ImVec2(400, 300)); // 幅400, 高さ300（お好みで変更）

			// テクスチャ変更のチェックボックス
			ImGui::Checkbox("useMonsterBall", &useMonsterBall);

			ImGui::DragFloat4("lightcolor", &directionalLightData->color.x, 0.01f);
			ImGui::DragFloat3("lightDirection", &directionalLightData->direction.x, 0.01f);
			ImGui::DragFloat("lightIntensity", &directionalLightData->intensity, 0.01f);

			// カメラ操作
			ImGui::DragFloat3("Rotate", &cameraTransform.rotate.x, 0.01f);
			ImGui::SliderFloat3("Translate", &cameraTransform.translate.x, -10.0f, 10.0f);

			ImGui::End();

			transform.rotate.y += 0.03f;

			Matrix4x4 worldMatrix = MakeAffineMatrix(transform);
			Matrix4x4 cameraMatrix = MakeAffineMatrix(cameraTransform);
			Matrix4x4 viewMatrix = Inverse(cameraMatrix);
			Matrix4x4 projectionMatrix = MakePerspectiveFovMatrix(0.45f, 16.0f / 9.0f, 0.1f, 100.0f);
			Matrix4x4 worldViewProjectionMatrix = Multiply(worldMatrix, Multiply(viewMatrix, projectionMatrix));
			wvpDate->WVP = worldViewProjectionMatrix;

			// sprite用のworldViewProjectionMatrixを作る
			Matrix4x4 worldMatrixSprite = MakeAffineMatrix(transformSprite);
			Matrix4x4 viewMatrixSprite = MakeIdentity4x4();
			Matrix4x4 projectionMatrixSprite = MakeOrthographicMatrix(0.0f, 0.0f, kClientWidth, kClientHeight, 0.0f, 100.0f);
			Matrix4x4 worldViewProjectionMatrixSprite = Multiply(worldMatrixSprite, Multiply(viewMatrixSprite, projectionMatrixSprite));
			*transformationMatrixDataSprite = worldViewProjectionMatrixSprite;

			///
			// ここまでゲームの処理
			///

			// ゲームの処理が終わったので描画処理前にImGUIの内部コマンドを生成
			ImGui::Render();

			// これから書き込むバックバッファのインデックスを取得
			UINT backBufferIndex = swapChain->GetCurrentBackBufferIndex();

			//=========================
			// TransitionBarrierを張る
			//=========================
			// TransitionBarrierの設定
			D3D12_RESOURCE_BARRIER barrier{};

			// 今回のバリアはTransition
			barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;

			// Noneにしておく
			barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;

			// バリアを張る対象のリソース。現在のバックバッファに対して行う
			barrier.Transition.pResource = swapChainResources[backBufferIndex];

			// 遷移前(現在)のResourceState
			barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;

			// 遷移後のResourceState
			barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;

			// TransitionBarrierを張る
			commandList->ResourceBarrier(1, &barrier);

			// バリア張り終了---------------

			// 描画先のRTVとDSVを設定する
			D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = dsvDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
			commandList->OMSetRenderTargets(1, &rtvHandles[backBufferIndex], false, &dsvHandle);

			// 指定した色で画面全体をクリア
			float clearColor[] = { 0.1f,0.25f,0.5f,1.0f };// 青っぽい色
			commandList->ClearRenderTargetView(rtvHandles[backBufferIndex], clearColor, 0, nullptr);

			// 描画用DescriptorHeapの設定
			ID3D12DescriptorHeap* descriptorHeaps[] = { srvDescriptorHeap };
			commandList->SetDescriptorHeaps(1, descriptorHeaps);

			//=========================
			// コマンドを積む
			//=========================
			commandList->RSSetViewports(1, &viewport);                // Viewportを設定
			commandList->RSSetScissorRects(1, &scissorRect);          // Scirssorを設定

			// RootSignatureを設定。PSOに設定してるけど別途設定が必要
			commandList->SetGraphicsRootSignature(rootSignature);
			commandList->SetPipelineState(graphicsPipelineState);     // PSOを設定
			commandList->IASetVertexBuffers(0, 1, &vertexBufferView); // VBVを設定
			commandList->IASetIndexBuffer(&indexBufferViewSphere);    // IBVを設定

			// 形状を設定。PSOに設定しているものとは別。同じものを設定。
			commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

			// 指定した深度で画面全体をクリアする
			commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

			// マテリアルCBufferの場所を設定
			commandList->SetGraphicsRootConstantBufferView(0, materialResource->GetGPUVirtualAddress());

			// wvp用のCBufferの場所を設定
			commandList->SetGraphicsRootConstantBufferView(1, wvpResource->GetGPUVirtualAddress());

			// SRVのDescriptorTableの先頭を設定。2はrootParameter[2]。useMonsterBallでテクスチャを切り替え
			commandList->SetGraphicsRootDescriptorTable(2, useMonsterBall ? textureSrvHandleGPU[1] : textureSrvHandleGPU[0]);

			// rootParameter[3]のところにライトのリソースを入れる。CPUに送る
			commandList->SetGraphicsRootConstantBufferView(3, directionalLightResource->GetGPUVirtualAddress());

			// 描画(drawCall/ドローコール)。3頂点で1つのインスタンス。
			// 1つ目の引数は頂点の数 球は1536
			//commandList->DrawInstanced(1536, 1, 0, 0);
			commandList->DrawIndexedInstanced(1536, 1, 0, 0, 0);

			// マテリアルCBufferの場所を設定.Spriteの前に設定しなおす
			commandList->SetGraphicsRootConstantBufferView(0, materialResourceSprite->GetGPUVirtualAddress());

			// spriteの描画。変更が必要なものだけ変更する。
			commandList->IASetVertexBuffers(0, 1, &vertexBufferViewSprite);

			//IBVを設定
			commandList->IASetIndexBuffer(&indexBufferViewSprite);

			// sprite用のCBufferの場所の設定
			commandList->SetGraphicsRootConstantBufferView(1, transformationMatrixResourceSprite->GetGPUVirtualAddress());

			// spriteの描画。vertexは4だけど格納したインデックスは6でインデックスで描画
			commandList->DrawIndexedInstanced(6, 1, 0, 0, 0);

			// 諸々の描画が終わってからImGUIの描画を行う(手前に出さなきゃいけないからねぇ)
			// 実際のCommandListのImGUIの描画コマンドを積む
			ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commandList);

			// 描画処理終了、状態遷移
			// 今回はRenderTrigerからPresentにする
			barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
			barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;

			// TransitionBarrierを張る
			commandList->ResourceBarrier(1, &barrier);

			// コマンドリストの内容を確定させる。全てのコマンドを詰んでからCloseすること!!
			hr = commandList->Close();
			assert(SUCCEEDED(hr));

			// GPUにコマンドリストの実行を行わせる
			ID3D12CommandList* commandLists[] = { commandList };
			commandQueue->ExecuteCommandLists(1, commandLists);

			// GPUとOSに画面の交換をするように通知する
			swapChain->Present(1, 0);

			//===================
			// GPUにシグナルを送る
			//===================
			// Fenceの値をインクリメント
			++fenceValue;

			// GPUがここまでたどり着いたときにFenceの値を指定した値に代入するようにSignalを送る
			commandQueue->Signal(fence, fenceValue);

			// Fenceの値が指定したSignal値にたどり着いているか確認する
			// GetCompletedValueの初期値はFence作成時に渡した初期値
			if (fence->GetCompletedValue() < fenceValue) {

				//指定したSignalにたどり着いていないのでたどり着くまで待つようにイベントを設定
				fence->SetEventOnCompletion(fenceValue, fenceEvent);

				// イベントを待つ
				WaitForSingleObject(fenceEvent, INFINITE);
			}

			// 次のフレーム用のコマンドリストを準備
			hr = commandAllocator->Reset();
			assert(SUCCEEDED(hr));
			hr = commandList->Reset(commandAllocator, nullptr);
			assert(SUCCEEDED(hr));

		}
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

	// 自作リソース
	transformationMatrixResourceSprite->Release();
	materialResourceSprite->Release();
	indexResourceSprite->Release();
	vertexResourceSprite->Release();
	wvpResource->Release();
	materialResource->Release();
	directionalLightResource->Release();
	indexResourceSphre->Release();
	vertexResource->Release();
	depthStencilResource->Release();
	textureResource[0]->Release();
	textureResource[1]->Release();
	intermediateResource[0]->Release();
	intermediateResource[1]->Release();

	// パイプライン関連
	graphicsPipelineState->Release();
	signatureBlob->Release();
	if (errorBlob) { errorBlob->Release(); }
	rootSignature->Release();
	pixelShaderBlob->Release();
	vertexShaderBlob->Release();

	// dxc関連
	includeHandler->Release();
	dxcCompiler->Release();
	dxcUtils->Release();

	// Fence関連
	CloseHandle(fenceEvent);
	fence->Release();

	// ヒープ
	dsvDescriptorHeap->Release();
	srvDescriptorHeap->Release();
	rtvDescriptorHeap->Release();
	rtvDescriptorHeap = nullptr;

	// SwapChain
	swapChainResources[0]->Release();
	swapChainResources[1]->Release();
	swapChain->Release();

	// コマンド
	commandList->Release();
	commandList = nullptr;

	commandAllocator->Release();

	commandQueue->Release();
	commandQueue = nullptr;

	assert(commandList == nullptr);
	assert(commandQueue == nullptr);
	assert(rtvDescriptorHeap == nullptr);
	// 他にも必要なリソースがあれば確認

	// Device
	device->Release();

	// Adapter
	useAdapter->Release();

	// Factory
	dxgiFactory->Release();

	// Debug
#ifdef _DEBUG
	debugController->Release();
#endif

	CloseWindow(hwnd);

#ifdef _DEBUG
	debugController->Release();
#endif // _DEBUG
	CloseWindow(hwnd);

	//======================
	// ReportLiveObjects
	//======================
	// リソースリークチェック
	IDXGIDebug1* debug;
	if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&debug)))) {
		debug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_ALL);
		debug->ReportLiveObjects(DXGI_DEBUG_APP, DXGI_DEBUG_RLO_ALL);
		debug->ReportLiveObjects(DXGI_DEBUG_D3D12, DXGI_DEBUG_RLO_ALL);
		debug->Release();
	}

	// 警告時に止まる
	infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, true);

	return 0;
}

//============================
// 関数の定義
//============================

std::wstring ConvertString(const std::string& str)
{
	if (str.empty()) {
		return std::wstring();
	}

	auto sizeNeeded = MultiByteToWideChar(CP_UTF8, 0, reinterpret_cast<const char*>(&str[0]), static_cast<int>(str.size()), NULL, 0);
	if (sizeNeeded == 0) {
		return std::wstring();
	}
	std::wstring result(sizeNeeded, 0);
	MultiByteToWideChar(CP_UTF8, 0, reinterpret_cast<const char*>(&str[0]), static_cast<int>(str.size()), &result[0], sizeNeeded);
	return result;

	return std::wstring();
}

std::string ConvertString(const std::wstring& str)
{
	if (str.empty()) {
		return std::string();
	}

	auto sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), NULL, 0, NULL, NULL);
	if (sizeNeeded == 0) {
		return std::string();
	}
	std::string result(sizeNeeded, 0);
	WideCharToMultiByte(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), result.data(), sizeNeeded, NULL, NULL);
	return result;
	return std::string();
}

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

ID3D12Resource* CreateBufferResource(ID3D12Device* device, size_t sizeInBytes) {
	//=========================
	// VertexResourceを生成
	//=========================
	// 頂点リソース用のヒープ設定
	D3D12_HEAP_PROPERTIES uploadHeapProperties{};
	uploadHeapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;

	// 頂点リソースの設定
	D3D12_RESOURCE_DESC vertexResourceDesc{};

	// バッファリソース。テクスチャの場合はまた別の設定をする
	vertexResourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	vertexResourceDesc.Width = sizeInBytes;// リソースのサイズ。

	// バッファの場合はこれらを1にする決まり
	vertexResourceDesc.Height = 1;
	vertexResourceDesc.DepthOrArraySize = 1;
	vertexResourceDesc.MipLevels = 1;
	vertexResourceDesc.SampleDesc.Count = 1;

	// バッファの場合の儀式
	vertexResourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

	// 実際に頂点リソースを作る
	ID3D12Resource* vertexResource = nullptr;

	HRESULT hr = device->CreateCommittedResource(&uploadHeapProperties, D3D12_HEAP_FLAG_NONE,
		&vertexResourceDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
		IID_PPV_ARGS(&vertexResource));

	assert(SUCCEEDED(hr));

	return vertexResource;
}

ID3D12DescriptorHeap* CreateDescriptorHeap(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE heapType, UINT numDescriptor, bool shaderVicible)
{
	ID3D12DescriptorHeap* descriptorHeap = nullptr;
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

ID3D12Resource* CreateTextureResource(ID3D12Device* device, const DirectX::TexMetadata& metadata)
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
	ID3D12Resource* resource = nullptr;
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

void UploadTextureData(ID3D12Resource* texture, const DirectX::ScratchImage& mipImages)
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

ID3D12Resource* UploadTextureData(ID3D12Resource* texture, const DirectX::ScratchImage& mipImages, ID3D12Device* device, ID3D12GraphicsCommandList* commandList)
{
	std::vector<D3D12_SUBRESOURCE_DATA> subresources;
	DirectX::PrepareUpload(device, mipImages.GetImages(), mipImages.GetImageCount(), mipImages.GetMetadata(), subresources);
	uint64_t intermediateSize = GetRequiredIntermediateSize(texture, 0, UINT(subresources.size()));
	ID3D12Resource* intermediateResource = CreateBufferResource(device, intermediateSize);
	UpdateSubresources(commandList, texture, intermediateResource, 0, 0, UINT(subresources.size()), subresources.data());

	// Textureへの転送後は利用できるよう、D3D12_RESOURCE_STATE_COPY_DESTからD3D12_RESOURCE_STATE_GENERIC_READへResourceStateを変更
	D3D12_RESOURCE_BARRIER barrier{};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	barrier.Transition.pResource = texture;
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_GENERIC_READ;
	commandList->ResourceBarrier(1, &barrier);

	return intermediateResource;
}

ID3D12Resource* CreateDepthStencilTextureResource(ID3D12Device* device, int32_t width, int32_t height)
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
	ID3D12Resource* resource = nullptr;
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


//===============================
// MT3でも使う関数
//===============================
Matrix4x4 Multiply(Matrix4x4 matrix1, Matrix4x4 matrix2)
{
	Matrix4x4 resoultMatrix4x4;

	resoultMatrix4x4.m[0][0] = matrix1.m[0][0] * matrix2.m[0][0] + matrix1.m[0][1] * matrix2.m[1][0] + matrix1.m[0][2] * matrix2.m[2][0] + matrix1.m[0][3] * matrix2.m[3][0];
	resoultMatrix4x4.m[0][1] = matrix1.m[0][0] * matrix2.m[0][1] + matrix1.m[0][1] * matrix2.m[1][1] + matrix1.m[0][2] * matrix2.m[2][1] + matrix1.m[0][3] * matrix2.m[3][1];
	resoultMatrix4x4.m[0][2] = matrix1.m[0][0] * matrix2.m[0][2] + matrix1.m[0][1] * matrix2.m[1][2] + matrix1.m[0][2] * matrix2.m[2][2] + matrix1.m[0][3] * matrix2.m[3][2];
	resoultMatrix4x4.m[0][3] = matrix1.m[0][0] * matrix2.m[0][3] + matrix1.m[0][1] * matrix2.m[1][3] + matrix1.m[0][2] * matrix2.m[2][3] + matrix1.m[0][3] * matrix2.m[3][3];

	resoultMatrix4x4.m[1][0] = matrix1.m[1][0] * matrix2.m[0][0] + matrix1.m[1][1] * matrix2.m[1][0] + matrix1.m[1][2] * matrix2.m[2][0] + matrix1.m[1][3] * matrix2.m[3][0];
	resoultMatrix4x4.m[1][1] = matrix1.m[1][0] * matrix2.m[0][1] + matrix1.m[1][1] * matrix2.m[1][1] + matrix1.m[1][2] * matrix2.m[2][1] + matrix1.m[1][3] * matrix2.m[3][1];
	resoultMatrix4x4.m[1][2] = matrix1.m[1][0] * matrix2.m[0][2] + matrix1.m[1][1] * matrix2.m[1][2] + matrix1.m[1][2] * matrix2.m[2][2] + matrix1.m[1][3] * matrix2.m[3][2];
	resoultMatrix4x4.m[1][3] = matrix1.m[1][0] * matrix2.m[0][3] + matrix1.m[1][1] * matrix2.m[1][3] + matrix1.m[1][2] * matrix2.m[2][3] + matrix1.m[1][3] * matrix2.m[3][3];

	resoultMatrix4x4.m[2][0] = matrix1.m[2][0] * matrix2.m[0][0] + matrix1.m[2][1] * matrix2.m[1][0] + matrix1.m[2][2] * matrix2.m[2][0] + matrix1.m[2][3] * matrix2.m[3][0];
	resoultMatrix4x4.m[2][1] = matrix1.m[2][0] * matrix2.m[0][1] + matrix1.m[2][1] * matrix2.m[1][1] + matrix1.m[2][2] * matrix2.m[2][1] + matrix1.m[2][3] * matrix2.m[3][1];
	resoultMatrix4x4.m[2][2] = matrix1.m[2][0] * matrix2.m[0][2] + matrix1.m[2][1] * matrix2.m[1][2] + matrix1.m[2][2] * matrix2.m[2][2] + matrix1.m[2][3] * matrix2.m[3][2];
	resoultMatrix4x4.m[2][3] = matrix1.m[2][0] * matrix2.m[0][3] + matrix1.m[2][1] * matrix2.m[1][3] + matrix1.m[2][2] * matrix2.m[2][3] + matrix1.m[2][3] * matrix2.m[3][3];

	resoultMatrix4x4.m[3][0] = matrix1.m[3][0] * matrix2.m[0][0] + matrix1.m[3][1] * matrix2.m[1][0] + matrix1.m[3][2] * matrix2.m[2][0] + matrix1.m[3][3] * matrix2.m[3][0];
	resoultMatrix4x4.m[3][1] = matrix1.m[3][0] * matrix2.m[0][1] + matrix1.m[3][1] * matrix2.m[1][1] + matrix1.m[3][2] * matrix2.m[2][1] + matrix1.m[3][3] * matrix2.m[3][1];
	resoultMatrix4x4.m[3][2] = matrix1.m[3][0] * matrix2.m[0][2] + matrix1.m[3][1] * matrix2.m[1][2] + matrix1.m[3][2] * matrix2.m[2][2] + matrix1.m[3][3] * matrix2.m[3][2];
	resoultMatrix4x4.m[3][3] = matrix1.m[3][0] * matrix2.m[0][3] + matrix1.m[3][1] * matrix2.m[1][3] + matrix1.m[3][2] * matrix2.m[2][3] + matrix1.m[3][3] * matrix2.m[3][3];

	return resoultMatrix4x4;
}

Matrix4x4 MakeIdentity4x4()
{
	Matrix4x4 identityMatrix4x4;

	identityMatrix4x4.m[0][0] = 1.0f;
	identityMatrix4x4.m[0][1] = 0.0f;
	identityMatrix4x4.m[0][2] = 0.0f;
	identityMatrix4x4.m[0][3] = 0.0f;
	identityMatrix4x4.m[1][0] = 0.0f;
	identityMatrix4x4.m[1][1] = 1.0f;
	identityMatrix4x4.m[1][2] = 0.0f;
	identityMatrix4x4.m[1][3] = 0.0f;
	identityMatrix4x4.m[2][0] = 0.0f;
	identityMatrix4x4.m[2][1] = 0.0f;
	identityMatrix4x4.m[2][2] = 1.0f;
	identityMatrix4x4.m[2][3] = 0.0f;
	identityMatrix4x4.m[3][0] = 0.0f;
	identityMatrix4x4.m[3][1] = 0.0f;
	identityMatrix4x4.m[3][2] = 0.0f;
	identityMatrix4x4.m[3][3] = 1.0f;

	return identityMatrix4x4;
}

Vector3 TransformCoordinate(const Vector3& vector3, const Matrix4x4& matrix4x4)
{
	Vector3 resoultVector3;

	resoultVector3.x = vector3.x * matrix4x4.m[0][0] + vector3.y * matrix4x4.m[1][0] + vector3.z * matrix4x4.m[2][0] + 1.0f * matrix4x4.m[3][0];
	resoultVector3.y = vector3.x * matrix4x4.m[0][1] + vector3.y * matrix4x4.m[1][1] + vector3.z * matrix4x4.m[2][1] + 1.0f * matrix4x4.m[3][1];
	resoultVector3.z = vector3.x * matrix4x4.m[0][2] + vector3.y * matrix4x4.m[1][2] + vector3.z * matrix4x4.m[2][2] + 1.0f * matrix4x4.m[3][2];

	float w = vector3.x * matrix4x4.m[0][3] + vector3.y * matrix4x4.m[1][3] + vector3.z * matrix4x4.m[2][3] + 1.0f * matrix4x4.m[3][3];
	assert(w != 0.0f);// ベクトルに対して基本的な操作を行う行列でwが0になることはありえないので0なら停止

	resoultVector3.x /= w;
	resoultVector3.y /= w;
	resoultVector3.z /= w;

	return resoultVector3;
}

Matrix4x4 MakeAffineMatrix(Transform& transform)
{
	//====================
	// 拡縮の行列の作成
	//====================
	Matrix4x4 scaleMatrix4x4;
	scaleMatrix4x4.m[0][0] = transform.scale.x;
	scaleMatrix4x4.m[0][1] = 0.0f;
	scaleMatrix4x4.m[0][2] = 0.0f;
	scaleMatrix4x4.m[0][3] = 0.0f;

	scaleMatrix4x4.m[1][0] = 0.0f;
	scaleMatrix4x4.m[1][1] = transform.scale.y;
	scaleMatrix4x4.m[1][2] = 0.0f;
	scaleMatrix4x4.m[1][3] = 0.0f;

	scaleMatrix4x4.m[2][0] = 0.0f;
	scaleMatrix4x4.m[2][1] = 0.0f;
	scaleMatrix4x4.m[2][2] = transform.scale.z;
	scaleMatrix4x4.m[2][3] = 0.0f;

	scaleMatrix4x4.m[3][0] = 0.0f;
	scaleMatrix4x4.m[3][1] = 0.0f;
	scaleMatrix4x4.m[3][2] = 0.0f;
	scaleMatrix4x4.m[3][3] = 1.0f;

	//===================
	// 回転の行列の作成
	//===================
	// Xの回転行列
	Matrix4x4 rotateMatrixX;
	rotateMatrixX.m[0][0] = 1.0f;
	rotateMatrixX.m[0][1] = 0.0f;
	rotateMatrixX.m[0][2] = 0.0f;
	rotateMatrixX.m[0][3] = 0.0f;

	rotateMatrixX.m[1][0] = 0.0f;
	rotateMatrixX.m[1][1] = cosf(transform.rotate.x);
	rotateMatrixX.m[1][2] = sinf(transform.rotate.x);
	rotateMatrixX.m[1][3] = 0.0f;

	rotateMatrixX.m[2][0] = 0.0f;
	rotateMatrixX.m[2][1] = -sinf(transform.rotate.x);
	rotateMatrixX.m[2][2] = cosf(transform.rotate.x);
	rotateMatrixX.m[2][3] = 0.0f;

	rotateMatrixX.m[3][0] = 0.0f;
	rotateMatrixX.m[3][1] = 0.0f;
	rotateMatrixX.m[3][2] = 0.0f;
	rotateMatrixX.m[3][3] = 1.0f;

	// Yの回転行列
	Matrix4x4 rotateMatrixY;
	rotateMatrixY.m[0][0] = cosf(transform.rotate.y);
	rotateMatrixY.m[0][1] = 0.0f;
	rotateMatrixY.m[0][2] = -sinf(transform.rotate.y);
	rotateMatrixY.m[0][3] = 0.0f;

	rotateMatrixY.m[1][0] = 0.0f;
	rotateMatrixY.m[1][1] = 1.0f;
	rotateMatrixY.m[1][2] = 0.0f;
	rotateMatrixY.m[1][3] = 0.0f;

	rotateMatrixY.m[2][0] = sinf(transform.rotate.y);
	rotateMatrixY.m[2][1] = 0.0f;
	rotateMatrixY.m[2][2] = cosf(transform.rotate.y);
	rotateMatrixY.m[2][3] = 0.0f;

	rotateMatrixY.m[3][0] = 0.0f;
	rotateMatrixY.m[3][1] = 0.0f;
	rotateMatrixY.m[3][2] = 0.0f;
	rotateMatrixY.m[3][3] = 1.0f;

	// Zの回転行列
	Matrix4x4 rotateMatrixZ;
	rotateMatrixZ.m[0][0] = cosf(transform.rotate.z);
	rotateMatrixZ.m[0][1] = sinf(transform.rotate.z);
	rotateMatrixZ.m[0][2] = 0.0f;
	rotateMatrixZ.m[0][3] = 0.0f;

	rotateMatrixZ.m[1][0] = -sinf(transform.rotate.z);
	rotateMatrixZ.m[1][1] = cosf(transform.rotate.z);
	rotateMatrixZ.m[1][2] = 0.0f;
	rotateMatrixZ.m[1][3] = 0.0f;

	rotateMatrixZ.m[2][0] = 0.0f;
	rotateMatrixZ.m[2][1] = 0.0f;
	rotateMatrixZ.m[2][2] = 1.0f;
	rotateMatrixZ.m[2][3] = 0.0f;

	rotateMatrixZ.m[3][0] = 0.0f;
	rotateMatrixZ.m[3][1] = 0.0f;
	rotateMatrixZ.m[3][2] = 0.0f;
	rotateMatrixZ.m[3][3] = 1.0f;

	// 回転行列の作成
	Matrix4x4 rotateMatrix4x4;

	rotateMatrix4x4 = Multiply(rotateMatrixX, Multiply(rotateMatrixY, rotateMatrixZ));

	//==================
	// 移動の行列の作成
	//==================
	Matrix4x4 translateMatrix4x4;
	translateMatrix4x4.m[0][0] = 1.0f;
	translateMatrix4x4.m[0][1] = 0.0f;
	translateMatrix4x4.m[0][2] = 0.0f;
	translateMatrix4x4.m[0][3] = 0.0f;

	translateMatrix4x4.m[1][0] = 0.0f;
	translateMatrix4x4.m[1][1] = 1.0f;
	translateMatrix4x4.m[1][2] = 0.0f;
	translateMatrix4x4.m[1][3] = 0.0f;

	translateMatrix4x4.m[2][0] = 0.0f;
	translateMatrix4x4.m[2][1] = 0.0f;
	translateMatrix4x4.m[2][2] = 1.0f;
	translateMatrix4x4.m[2][3] = 0.0f;

	translateMatrix4x4.m[3][0] = transform.translate.x;
	translateMatrix4x4.m[3][1] = transform.translate.y;
	translateMatrix4x4.m[3][2] = transform.translate.z;
	translateMatrix4x4.m[3][3] = 1.0f;

	//====================
	// アフィン行列の作成
	//====================
	// 上で作った行列からアフィン行列を作る
	Matrix4x4 affineMatrix4x4;

	affineMatrix4x4 = Multiply(translateMatrix4x4, Multiply(rotateMatrix4x4, scaleMatrix4x4));

	return  affineMatrix4x4;
}

Matrix4x4 MakePerspectiveFovMatrix(float fovY, float aspectRatio, float nearClip, float farClip)
{
	Matrix4x4 perspectiveFovMatrix;

	perspectiveFovMatrix.m[0][0] = 1.0f / aspectRatio * Cotangent(fovY / 2.0f);
	perspectiveFovMatrix.m[0][1] = 0.0f;
	perspectiveFovMatrix.m[0][2] = 0.0f;
	perspectiveFovMatrix.m[0][3] = 0.0f;

	perspectiveFovMatrix.m[1][0] = 0.0f;
	perspectiveFovMatrix.m[1][1] = Cotangent(fovY / 2.0f);
	perspectiveFovMatrix.m[1][2] = 0.0f;
	perspectiveFovMatrix.m[1][3] = 0.0f;

	perspectiveFovMatrix.m[2][0] = 0.0f;
	perspectiveFovMatrix.m[2][1] = 0.0f;
	perspectiveFovMatrix.m[2][2] = farClip / (farClip - nearClip);
	perspectiveFovMatrix.m[2][3] = 1.0f;

	perspectiveFovMatrix.m[3][0] = 0.0f;
	perspectiveFovMatrix.m[3][1] = 0.0f;
	perspectiveFovMatrix.m[3][2] = (-nearClip * farClip) / (farClip - nearClip);
	perspectiveFovMatrix.m[3][3] = 0.0f;

	return perspectiveFovMatrix;
}

Matrix4x4 MakeOrthographicMatrix(float left, float top, float right, float bottom, float nearClip, float farClip)
{
	Matrix4x4 orthographicMatrix;

	orthographicMatrix.m[0][0] = 2.0f / (right - left);
	orthographicMatrix.m[0][1] = 0.0f;
	orthographicMatrix.m[0][2] = 0.0f;
	orthographicMatrix.m[0][3] = 0.0f;

	orthographicMatrix.m[1][0] = 0.0f;
	orthographicMatrix.m[1][1] = 2.0f / (top - bottom);
	orthographicMatrix.m[1][2] = 0.0f;
	orthographicMatrix.m[1][3] = 0.0f;

	orthographicMatrix.m[2][0] = 0.0f;
	orthographicMatrix.m[2][1] = 0.0f;
	orthographicMatrix.m[2][2] = 1.0f / (farClip - nearClip);
	orthographicMatrix.m[2][3] = 0.0f;

	orthographicMatrix.m[3][0] = (left + right) / (left - right);
	orthographicMatrix.m[3][1] = (top + bottom) / (bottom - top);
	orthographicMatrix.m[3][2] = (nearClip) / (nearClip - farClip);
	orthographicMatrix.m[3][3] = 1.0f;

	return orthographicMatrix;
}

Matrix4x4 Inverse(Matrix4x4 matrix4x4)
{
	// 行列式|A|を求める
	float bottom =
		(matrix4x4.m[0][0] * matrix4x4.m[1][1] * matrix4x4.m[2][2] * matrix4x4.m[3][3])
		+ (matrix4x4.m[0][0] * matrix4x4.m[1][2] * matrix4x4.m[2][3] * matrix4x4.m[3][1])
		+ (matrix4x4.m[0][0] * matrix4x4.m[1][3] * matrix4x4.m[2][1] * matrix4x4.m[3][2])
		- (matrix4x4.m[0][0] * matrix4x4.m[1][3] * matrix4x4.m[2][2] * matrix4x4.m[3][1])
		- (matrix4x4.m[0][0] * matrix4x4.m[1][2] * matrix4x4.m[2][1] * matrix4x4.m[3][3])
		- (matrix4x4.m[0][0] * matrix4x4.m[1][1] * matrix4x4.m[2][3] * matrix4x4.m[3][2])
		- (matrix4x4.m[0][1] * matrix4x4.m[1][0] * matrix4x4.m[2][2] * matrix4x4.m[3][3])
		- (matrix4x4.m[0][2] * matrix4x4.m[1][0] * matrix4x4.m[2][3] * matrix4x4.m[3][1])
		- (matrix4x4.m[0][3] * matrix4x4.m[1][0] * matrix4x4.m[2][1] * matrix4x4.m[3][2])
		+ (matrix4x4.m[0][3] * matrix4x4.m[1][0] * matrix4x4.m[2][2] * matrix4x4.m[3][1])
		+ (matrix4x4.m[0][2] * matrix4x4.m[1][0] * matrix4x4.m[2][1] * matrix4x4.m[3][3])
		+ (matrix4x4.m[0][1] * matrix4x4.m[1][0] * matrix4x4.m[2][3] * matrix4x4.m[3][2])
		+ (matrix4x4.m[0][1] * matrix4x4.m[1][2] * matrix4x4.m[2][0] * matrix4x4.m[3][3])
		+ (matrix4x4.m[0][2] * matrix4x4.m[1][3] * matrix4x4.m[2][0] * matrix4x4.m[3][1])
		+ (matrix4x4.m[0][3] * matrix4x4.m[1][1] * matrix4x4.m[2][0] * matrix4x4.m[3][2])
		- (matrix4x4.m[0][3] * matrix4x4.m[1][2] * matrix4x4.m[2][0] * matrix4x4.m[3][1])
		- (matrix4x4.m[0][2] * matrix4x4.m[1][1] * matrix4x4.m[2][0] * matrix4x4.m[3][3])
		- (matrix4x4.m[0][1] * matrix4x4.m[1][3] * matrix4x4.m[2][0] * matrix4x4.m[3][2])
		- (matrix4x4.m[0][1] * matrix4x4.m[1][2] * matrix4x4.m[2][3] * matrix4x4.m[3][0])
		- (matrix4x4.m[0][2] * matrix4x4.m[1][3] * matrix4x4.m[2][1] * matrix4x4.m[3][0])
		- (matrix4x4.m[0][3] * matrix4x4.m[1][1] * matrix4x4.m[2][2] * matrix4x4.m[3][0])
		+ (matrix4x4.m[0][3] * matrix4x4.m[1][2] * matrix4x4.m[2][1] * matrix4x4.m[3][0])
		+ (matrix4x4.m[0][2] * matrix4x4.m[1][1] * matrix4x4.m[2][3] * matrix4x4.m[3][0])
		+ (matrix4x4.m[0][1] * matrix4x4.m[1][3] * matrix4x4.m[2][2] * matrix4x4.m[3][0]);

	Matrix4x4 resoultMatrix;

	// 1行目
	resoultMatrix.m[0][0] = 1.0f / bottom * (
		(matrix4x4.m[1][1] * matrix4x4.m[2][2] * matrix4x4.m[3][3])
		+ (matrix4x4.m[1][2] * matrix4x4.m[2][3] * matrix4x4.m[3][1])
		+ (matrix4x4.m[1][3] * matrix4x4.m[2][1] * matrix4x4.m[3][2])
		- (matrix4x4.m[1][3] * matrix4x4.m[2][2] * matrix4x4.m[3][1])
		- (matrix4x4.m[1][2] * matrix4x4.m[2][1] * matrix4x4.m[3][3])
		- (matrix4x4.m[1][1] * matrix4x4.m[2][3] * matrix4x4.m[3][2]));

	resoultMatrix.m[0][1] = 1.0f / bottom * (
		-(matrix4x4.m[0][1] * matrix4x4.m[2][2] * matrix4x4.m[3][3])
		- (matrix4x4.m[0][2] * matrix4x4.m[2][3] * matrix4x4.m[3][1])
		- (matrix4x4.m[0][3] * matrix4x4.m[2][1] * matrix4x4.m[3][2])
		+ (matrix4x4.m[0][3] * matrix4x4.m[2][2] * matrix4x4.m[3][1])
		+ (matrix4x4.m[0][2] * matrix4x4.m[2][1] * matrix4x4.m[3][3])
		+ (matrix4x4.m[0][1] * matrix4x4.m[2][3] * matrix4x4.m[3][2]));

	resoultMatrix.m[0][2] = 1.0f / bottom * (
		(matrix4x4.m[0][1] * matrix4x4.m[1][2] * matrix4x4.m[3][3])
		+ (matrix4x4.m[0][2] * matrix4x4.m[1][3] * matrix4x4.m[3][1])
		+ (matrix4x4.m[0][3] * matrix4x4.m[1][1] * matrix4x4.m[3][2])
		- (matrix4x4.m[0][3] * matrix4x4.m[1][2] * matrix4x4.m[3][1])
		- (matrix4x4.m[0][2] * matrix4x4.m[1][1] * matrix4x4.m[3][3])
		- (matrix4x4.m[0][1] * matrix4x4.m[1][3] * matrix4x4.m[3][2]));

	resoultMatrix.m[0][3] = 1.0f / bottom * (
		-(matrix4x4.m[0][1] * matrix4x4.m[1][2] * matrix4x4.m[2][3])
		- (matrix4x4.m[0][2] * matrix4x4.m[1][3] * matrix4x4.m[2][1])
		- (matrix4x4.m[0][3] * matrix4x4.m[1][1] * matrix4x4.m[2][2])
		+ (matrix4x4.m[0][3] * matrix4x4.m[1][2] * matrix4x4.m[2][1])
		+ (matrix4x4.m[0][2] * matrix4x4.m[1][1] * matrix4x4.m[2][3])
		+ (matrix4x4.m[0][1] * matrix4x4.m[1][3] * matrix4x4.m[2][2]));

	// 2行目
	resoultMatrix.m[1][0] = 1.0f / bottom * (
		-(matrix4x4.m[1][0] * matrix4x4.m[2][2] * matrix4x4.m[3][3])
		- (matrix4x4.m[1][2] * matrix4x4.m[2][3] * matrix4x4.m[3][0])
		- (matrix4x4.m[1][3] * matrix4x4.m[2][0] * matrix4x4.m[3][2])
		+ (matrix4x4.m[1][3] * matrix4x4.m[2][2] * matrix4x4.m[3][0])
		+ (matrix4x4.m[1][2] * matrix4x4.m[2][0] * matrix4x4.m[3][3])
		+ (matrix4x4.m[1][0] * matrix4x4.m[2][3] * matrix4x4.m[3][2]));

	resoultMatrix.m[1][1] = 1.0f / bottom * (
		(matrix4x4.m[0][0] * matrix4x4.m[2][2] * matrix4x4.m[3][3])
		+ (matrix4x4.m[0][2] * matrix4x4.m[2][3] * matrix4x4.m[3][0])
		+ (matrix4x4.m[0][3] * matrix4x4.m[2][0] * matrix4x4.m[3][2])
		- (matrix4x4.m[0][3] * matrix4x4.m[2][2] * matrix4x4.m[3][0])
		- (matrix4x4.m[0][2] * matrix4x4.m[2][0] * matrix4x4.m[3][3])
		- (matrix4x4.m[0][0] * matrix4x4.m[2][3] * matrix4x4.m[3][2]));

	resoultMatrix.m[1][2] = 1.0f / bottom * (
		-(matrix4x4.m[0][0] * matrix4x4.m[1][2] * matrix4x4.m[3][3])
		- (matrix4x4.m[0][2] * matrix4x4.m[1][3] * matrix4x4.m[3][0])
		- (matrix4x4.m[0][3] * matrix4x4.m[1][0] * matrix4x4.m[3][2])
		+ (matrix4x4.m[0][3] * matrix4x4.m[1][2] * matrix4x4.m[3][0])
		+ (matrix4x4.m[0][2] * matrix4x4.m[1][0] * matrix4x4.m[3][3])
		+ (matrix4x4.m[0][0] * matrix4x4.m[1][3] * matrix4x4.m[3][2]));

	resoultMatrix.m[1][3] = 1.0f / bottom * (
		(matrix4x4.m[0][0] * matrix4x4.m[1][2] * matrix4x4.m[2][3])
		+ (matrix4x4.m[0][2] * matrix4x4.m[1][3] * matrix4x4.m[2][0])
		+ (matrix4x4.m[0][3] * matrix4x4.m[1][0] * matrix4x4.m[2][2])
		- (matrix4x4.m[0][3] * matrix4x4.m[1][2] * matrix4x4.m[2][0])
		- (matrix4x4.m[0][2] * matrix4x4.m[1][0] * matrix4x4.m[2][3])
		- (matrix4x4.m[0][0] * matrix4x4.m[1][3] * matrix4x4.m[2][2]));

	// 3行目
	resoultMatrix.m[2][0] = 1.0f / bottom * (
		(matrix4x4.m[1][0] * matrix4x4.m[2][1] * matrix4x4.m[3][3])
		+ (matrix4x4.m[1][1] * matrix4x4.m[2][3] * matrix4x4.m[3][0])
		+ (matrix4x4.m[1][3] * matrix4x4.m[2][0] * matrix4x4.m[3][1])
		- (matrix4x4.m[1][3] * matrix4x4.m[2][1] * matrix4x4.m[3][0])
		- (matrix4x4.m[1][1] * matrix4x4.m[2][0] * matrix4x4.m[3][3])
		- (matrix4x4.m[1][0] * matrix4x4.m[2][3] * matrix4x4.m[3][1]));

	resoultMatrix.m[2][1] = 1.0f / bottom * (
		-(matrix4x4.m[0][0] * matrix4x4.m[2][1] * matrix4x4.m[3][3])
		- (matrix4x4.m[0][1] * matrix4x4.m[2][3] * matrix4x4.m[3][0])
		- (matrix4x4.m[0][3] * matrix4x4.m[2][0] * matrix4x4.m[3][1])
		+ (matrix4x4.m[0][3] * matrix4x4.m[2][1] * matrix4x4.m[3][0])
		+ (matrix4x4.m[0][1] * matrix4x4.m[2][0] * matrix4x4.m[3][3])
		+ (matrix4x4.m[0][0] * matrix4x4.m[2][3] * matrix4x4.m[3][1]));

	resoultMatrix.m[2][2] = 1.0f / bottom * (
		(matrix4x4.m[0][0] * matrix4x4.m[1][1] * matrix4x4.m[3][3])
		+ (matrix4x4.m[0][1] * matrix4x4.m[1][3] * matrix4x4.m[3][0])
		+ (matrix4x4.m[0][3] * matrix4x4.m[1][0] * matrix4x4.m[3][1])
		- (matrix4x4.m[0][3] * matrix4x4.m[1][1] * matrix4x4.m[3][0])
		- (matrix4x4.m[0][1] * matrix4x4.m[1][0] * matrix4x4.m[3][3])
		- (matrix4x4.m[0][0] * matrix4x4.m[1][3] * matrix4x4.m[3][1]));

	resoultMatrix.m[2][3] = 1.0f / bottom * (
		-(matrix4x4.m[0][0] * matrix4x4.m[1][1] * matrix4x4.m[2][3])
		- (matrix4x4.m[0][1] * matrix4x4.m[1][3] * matrix4x4.m[2][0])
		- (matrix4x4.m[0][3] * matrix4x4.m[1][0] * matrix4x4.m[2][1])
		+ (matrix4x4.m[0][3] * matrix4x4.m[1][1] * matrix4x4.m[2][0])
		+ (matrix4x4.m[0][1] * matrix4x4.m[1][0] * matrix4x4.m[2][3])
		+ (matrix4x4.m[0][0] * matrix4x4.m[1][3] * matrix4x4.m[2][1]));

	// 4行目
	resoultMatrix.m[3][0] = 1.0f / bottom * (
		-(matrix4x4.m[1][0] * matrix4x4.m[2][1] * matrix4x4.m[3][2])
		- (matrix4x4.m[1][1] * matrix4x4.m[2][2] * matrix4x4.m[3][0])
		- (matrix4x4.m[1][2] * matrix4x4.m[2][0] * matrix4x4.m[3][1])
		+ (matrix4x4.m[1][2] * matrix4x4.m[2][1] * matrix4x4.m[3][0])
		+ (matrix4x4.m[1][1] * matrix4x4.m[2][0] * matrix4x4.m[3][2])
		+ (matrix4x4.m[1][0] * matrix4x4.m[2][2] * matrix4x4.m[3][1]));

	resoultMatrix.m[3][1] = 1.0f / bottom * (
		(matrix4x4.m[0][0] * matrix4x4.m[2][1] * matrix4x4.m[3][2])
		+ (matrix4x4.m[0][1] * matrix4x4.m[2][2] * matrix4x4.m[3][0])
		+ (matrix4x4.m[0][2] * matrix4x4.m[2][0] * matrix4x4.m[3][1])
		- (matrix4x4.m[0][2] * matrix4x4.m[2][1] * matrix4x4.m[3][0])
		- (matrix4x4.m[0][1] * matrix4x4.m[2][0] * matrix4x4.m[3][2])
		- (matrix4x4.m[0][0] * matrix4x4.m[2][2] * matrix4x4.m[3][1]));

	resoultMatrix.m[3][2] = 1.0f / bottom * (
		-(matrix4x4.m[0][0] * matrix4x4.m[1][1] * matrix4x4.m[3][2])
		- (matrix4x4.m[0][1] * matrix4x4.m[1][2] * matrix4x4.m[3][0])
		- (matrix4x4.m[0][2] * matrix4x4.m[1][0] * matrix4x4.m[3][1])
		+ (matrix4x4.m[0][2] * matrix4x4.m[1][1] * matrix4x4.m[3][0])
		+ (matrix4x4.m[0][1] * matrix4x4.m[1][0] * matrix4x4.m[3][2])
		+ (matrix4x4.m[0][0] * matrix4x4.m[1][2] * matrix4x4.m[3][1]));

	resoultMatrix.m[3][3] = 1.0f / bottom * (
		(matrix4x4.m[0][0] * matrix4x4.m[1][1] * matrix4x4.m[2][2])
		+ (matrix4x4.m[0][1] * matrix4x4.m[1][2] * matrix4x4.m[2][0])
		+ (matrix4x4.m[0][2] * matrix4x4.m[1][0] * matrix4x4.m[2][1])
		- (matrix4x4.m[0][2] * matrix4x4.m[1][1] * matrix4x4.m[2][0])
		- (matrix4x4.m[0][1] * matrix4x4.m[1][0] * matrix4x4.m[2][2])
		- (matrix4x4.m[0][0] * matrix4x4.m[1][2] * matrix4x4.m[2][1]));

	return resoultMatrix;
}

void DrawSphere(const Vector3& center, float radius, const Matrix4x4& viewProjectionMatrix, const Matrix4x4& viewportMatrix, uint32_t color)
{
	const uint32_t kSubdivision = 20; //分割数
	const float kLatEvery = static_cast<float>(M_PI) / static_cast<float>(kSubdivision); // 緯度分割1つ分の角度 θd
	const float kLonEvery = static_cast<float>(2.0f * M_PI) / static_cast<float>(kSubdivision); // 経度分割1つ分の角度 φd

	// 緯度の方向に分割 -π/2~π/2
	for (uint32_t latIndex = 0; latIndex < kSubdivision; ++latIndex) {
		float lat = -static_cast<float>(M_PI) / 2.0f + kLatEvery * latIndex; // θ

		// 経度の方向に分割 θ~2π
		for (uint32_t lonIndex = 0; lonIndex < kSubdivision; ++lonIndex) {
			float lon = kLonEvery * lonIndex; // φ

			// 緯線
			Vector3 a = {
				center.x + radius * cosf(lat) * cosf(lon),
				center.y + radius * sinf(lat),
				center.z + radius * cosf(lat) * sinf(lon)
			};

			Vector3 b = {
				center.x + radius * cosf(lat + kLatEvery) * cosf(lon),
				center.y + radius * sinf(lat + kLatEvery),
				center.z + radius * cosf(lat + kLatEvery) * sinf(lon)
			};

			// 経線
			Vector3 c = {
				center.x + radius * cosf(lat) * cosf(lon + kLonEvery),
				center.y + radius * sinf(lat),
				center.z + radius * cosf(lat) * sinf(lon + kLonEvery)
			};

			Vector3 d = {
				center.x + radius * cosf(lat + kLatEvery) * cosf(lon + kLonEvery),
				center.y + radius * sinf(lat + kLatEvery),
				center.z + radius * cosf(lat + kLatEvery) * sinf(lon + kLonEvery)
			};

			a = TransformMatrix(TransformMatrix(a, viewProjectionMatrix), viewportMatrix);
			b = TransformMatrix(TransformMatrix(b, viewProjectionMatrix), viewportMatrix);
			c = TransformMatrix(TransformMatrix(c, viewProjectionMatrix), viewportMatrix);

		}
	}
}

Vector3 TransformMatrix(const Vector3& vector, const Matrix4x4& matrix)
{
	Vector3 resultVector3;

	resultVector3.x = vector.x * matrix.m[0][0] + vector.y * matrix.m[1][0] + vector.z * matrix.m[2][0] + 1.0f * matrix.m[3][0];
	resultVector3.y = vector.x * matrix.m[0][1] + vector.y * matrix.m[1][1] + vector.z * matrix.m[2][1] + 1.0f * matrix.m[3][1];
	resultVector3.z = vector.x * matrix.m[0][2] + vector.y * matrix.m[1][2] + vector.z * matrix.m[2][2] + 1.0f * matrix.m[3][2];

	float w = vector.x * matrix.m[0][3] + vector.y * matrix.m[1][3] + vector.z * matrix.m[2][3] + 1.0f * matrix.m[3][3];

	assert(w != 0.0f);

	resultVector3.x /= w;
	resultVector3.y /= w;
	resultVector3.z /= w;

	return resultVector3;
}
D3D12_CPU_DESCRIPTOR_HANDLE GetCPUDescriptorHandle(ID3D12DescriptorHeap* descriptorHeap, uint32_t descriptorSize, uint32_t index)
{
	D3D12_CPU_DESCRIPTOR_HANDLE handleCPU;

	handleCPU = descriptorHeap->GetCPUDescriptorHandleForHeapStart();
	handleCPU.ptr += (descriptorSize * index);

	return handleCPU;
}

D3D12_GPU_DESCRIPTOR_HANDLE GetGPUDescriptorHandle(ID3D12DescriptorHeap* descriptorHeap, uint32_t descriptorSize, uint32_t index)
{
	D3D12_GPU_DESCRIPTOR_HANDLE handleGPU;

	handleGPU = descriptorHeap->GetGPUDescriptorHandleForHeapStart();
	handleGPU.ptr += (descriptorSize * index);

	return handleGPU;
}