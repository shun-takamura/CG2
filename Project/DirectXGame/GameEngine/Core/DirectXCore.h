#pragma once
#include <wrl.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <cstdint>
#include <cassert>
#include <chrono>
#include <thread>
#include "ConvertStringClass.h"
#include "WindowsApplication.h"

#include "Log.h"
#include "ConvertString.h"
#include <d3d12.h>  
#include <dxcapi.h>

#include"SRVManager.h"

#pragma comment(lib, "winmm.lib")

/// <summary>
/// DirectX12 の基盤クラス。
/// Device / CommandQueue / SwapChain / RTV / DSV / Fence などの管理を担当。
/// </summary>
class DirectXCore {
public:
	/// <summary>
	/// DirectX12 の初期化を行う。
	/// </summary>
	/// <param name="hwnd">ウィンドウハンドル</param>
	/// <param name="width">クライアント領域の幅</param>
	/// <param name="height">クライアント領域の高さ</param>
	void Initialize(WindowsApplication* winApp);

	/// <summary>
	/// SRV(CBV/SRV/UAV) のディスクリプタサイズを取得。
	/// </summary>
	UINT GetSrvDescriptorSize() const {
	assert(device_);
	return device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	}

	/// <summary>
	/// 描画コマンドの積み始め。
	/// </summary>
	void BeginDraw();

	/// <summary>
	/// 深度バッファをクリア
	/// </summary>
	void ClearDepthBuffer();

	/// <summary>
	/// 描画コマンドの終了と Present。
	/// </summary>
	void EndDraw();

	/// <summary>
	/// 終了処理。
	/// </summary>
	void Finalize();

	/// <summary>
	/// デバイスの取得。
	/// </summary>
	ID3D12Device* GetDevice() const { return device_.Get(); }

	/// <summary>
	/// コマンドリストの取得。
	/// </summary>
	ID3D12GraphicsCommandList* GetCommandList() const { return commandList_.Get(); }

	/// <summary>
	/// スワップチェインのバックバッファ数を取得
	/// </summary>
	UINT GetBackBufferCount() const { return bufferCount_; }

	/// <summary>
	/// RTVフォーマットを取得
	/// </summary>
	DXGI_FORMAT GetRenderTargetFormat() const { return backBufferFormat_; }

	// DSVハンドルの取得
	D3D12_CPU_DESCRIPTOR_HANDLE GetDsvHandle() const {
		return dsvHeap_->GetCPUDescriptorHandleForHeapStart();
	}

	IDXGISwapChain4* GetSwapChain() const { return swapChain_.Get(); }
	ID3D12DescriptorHeap* GetDsvHeap() const { return dsvHeap_.Get(); }

	// デルタタイムの取得
	float GetDeltaTime() const { return deltaTime_; }

	// 固定/可変フレームレートの切り替え
	void SetUseFixedFrameRate(bool useFixed) { useFixedFrameRate_ = useFixed; }
	bool IsUsingFixedFrameRate() const { return useFixedFrameRate_; }

	Microsoft::WRL::ComPtr<ID3D12Resource> CreateBufferResource(size_t sizeInBytes);
	IDxcBlob* CompileShader(const std::wstring& filePath, const wchar_t* profile);

	// 最大テクスチャ枚数
	//static const uint32_t kMaxTextureCount;

	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> CreateDescriptorHeap(
		Microsoft::WRL::ComPtr<ID3D12Device> device,
		D3D12_DESCRIPTOR_HEAP_TYPE heapType,
		UINT numDescriptor,
		bool shaderVicible);

private:
	void CreateFactory();
	void CreateDevice();
	void CreateCommandObjects();
	void CreateSwapChain(HWND hwnd, int32_t width, int32_t height);
	void CreateRenderTargets();
	void CreateDepthStencilView(int32_t width, int32_t height);
	void CreateFenceObjects();

	// FPS固定用
	void InitializeFixFPS();
	void UpdateFixFPS();

	// 前回フレームの時刻
	std::chrono::steady_clock::time_point reference_;

private:
	Microsoft::WRL::ComPtr<IDXGIFactory7> factory_;
	Microsoft::WRL::ComPtr<IDXGISwapChain4> swapChain_;
	Microsoft::WRL::ComPtr<ID3D12Device> device_;
	Microsoft::WRL::ComPtr<ID3D12CommandQueue> commandQueue_;
	Microsoft::WRL::ComPtr<ID3D12CommandAllocator> commandAllocator_;
	Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> commandList_;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvHeap_;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> dsvHeap_;
	Microsoft::WRL::ComPtr<ID3D12Resource> backBuffers_[2];
	Microsoft::WRL::ComPtr<ID3D12Resource> depthStencilBuffer_;
	Microsoft::WRL::ComPtr<ID3D12Fence> fence_;

	Microsoft::WRL::ComPtr<IDxcUtils> dxcUtils_ = nullptr;
	Microsoft::WRL::ComPtr<IDxcCompiler3> dxcCompiler_ = nullptr;
	Microsoft::WRL::ComPtr<IDxcIncludeHandler> includeHandler_ = nullptr;

	// デルタタイム
	std::chrono::steady_clock::time_point lastFrameTime_;
	float deltaTime_ = 1.0f / 60.0f;
	bool useFixedFrameRate_ = true; // true: 固定, false: 可変

	HANDLE fenceEvent_ = nullptr;
	UINT frameIndex_ = 0;
	UINT rtvDescriptorSize_ = 0;
	uint64_t fenceValue_ = 0;
	WindowsApplication* winApp_ = nullptr;
	// スワップチェイン設定の記録用
	UINT bufferCount_ = 2;
	DXGI_FORMAT backBufferFormat_ = DXGI_FORMAT_R8G8B8A8_UNORM;

};
