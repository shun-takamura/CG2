#pragma once
#include <wrl.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <cstdint>
#include <cassert>
#include <chrono>
#include <functional>
#include <thread>
#include <utility>
#include <vector>
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

	/// <summary>
    /// SwapchainのRTVとビューポートを再設定する
    /// ポストエフェクトのマルチパス後に呼ぶ
    /// </summary>
	void RestoreSwapchainRenderTarget(ID3D12GraphicsCommandList* commandList);

	// DSVハンドルの取得
	D3D12_CPU_DESCRIPTOR_HANDLE GetDsvHandle() const {
		return dsvHeap_->GetCPUDescriptorHandleForHeapStart();
	}

	// READ_ONLY_DEPTH用のDSVハンドル取得
	D3D12_CPU_DESCRIPTOR_HANDLE GetReadOnlyDsvHandle() const {
		D3D12_CPU_DESCRIPTOR_HANDLE handle = dsvHeap_->GetCPUDescriptorHandleForHeapStart();
		handle.ptr += device_->GetDescriptorHandleIncrementSize(
			D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
		return handle;
	}

	IDXGISwapChain4* GetSwapChain() const { return swapChain_.Get(); }
	ID3D12DescriptorHeap* GetDsvHeap() const { return dsvHeap_.Get(); }

	// Depthをシェーダで読むためのSRV登録（SRVManager初期化後に呼ぶ）
	void RegisterDepthSRV(class SRVManager* srvManager);

	// Depth SRVのインデックス
	uint32_t GetDepthSRVIndex() const { return depthSrvIndex_; }

	// Depthリソースの状態を遷移する
	void TransitionDepthState(ID3D12GraphicsCommandList* commandList, D3D12_RESOURCE_STATES afterState);

	// デルタタイムの取得（生の値）
	float GetDeltaTime() const { return deltaTime_; }

	// タイムスケール適用済みデルタタイム
	float GetScaledDeltaTime() const { return deltaTime_ * timeScale_; }

	// グローバルタイムスケール
	float GetTimeScale() const { return timeScale_; }
	void SetTimeScale(float scale) { timeScale_ = (scale < 0.0f) ? 0.0f : scale; }

	// 固定/可変フレームレートの切り替え
	void SetUseFixedFrameRate(bool useFixed) { useFixedFrameRate_ = useFixed; }
	bool IsUsingFixedFrameRate() const { return useFixedFrameRate_; }

	// このフレーム末に Signal される予定の fenceValue（中間バッファの解放判定用）
	uint64_t GetNextFenceValue() const { return fenceValue_ + 1; }

	// GPU が完了したフェンス値（これ以下の fenceValue で記録された中間バッファは解放OK）
	uint64_t GetCompletedFenceValue() const { return fence_ ? fence_->GetCompletedValue() : 0; }

	Microsoft::WRL::ComPtr<ID3D12Resource> CreateBufferResource(size_t sizeInBytes);

	// UAV用のバッファResource（DEFAULT heap、ALLOW_UNORDERED_ACCESSフラグ、初期StateはCOMMON）
	Microsoft::WRL::ComPtr<ID3D12Resource> CreateUavBufferResource(size_t sizeInBytes);

	// DEFAULT heap バッファを作成し、initialData を UPLOAD 中間バッファ経由でコピーして
	// finalState に遷移させる。中間バッファは内部で TrackIntermediateResource に積まれる
	Microsoft::WRL::ComPtr<ID3D12Resource> CreateDefaultBuffer(
		size_t sizeInBytes,
		const void* initialData,
		ID3D12GraphicsCommandList* commandList,
		D3D12_RESOURCE_STATES finalState);

	// アップロード用中間バッファをこのフレームの fenceValue とペアで保持する
	// （次フレーム以降の TickIntermediateResources で GPU 完了後に解放される）
	void TrackIntermediateResource(Microsoft::WRL::ComPtr<ID3D12Resource> resource);

	// 完了済みフェンス値以下の中間バッファを解放（毎フレーム末に呼ぶ）
	void TickIntermediateResources();

	// 指定 fenceValue が GPU 上で完了したら callback を実行する。
	// DirectStorage のバッチ完了通知や、async ロードの GPUReady 状態遷移等に使う想定。
	// 現状はメインスレッドからのみ呼ばれる前提（必要になったら mutex 化）。
	void EnqueueOnFenceComplete(uint64_t fenceValue, std::function<void()> callback);

	// 完了済みフェンス値以下に登録された callback を全て実行する（毎フレーム末に呼ぶ）
	void TickPendingCallbacks();

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
	// Depth SRVのインデックス（SRVManagerに登録）
	uint32_t depthSrvIndex_ = 0;
	// Depthリソースの現在の状態
	D3D12_RESOURCE_STATES depthState_ = D3D12_RESOURCE_STATE_DEPTH_WRITE;
	Microsoft::WRL::ComPtr<ID3D12Fence> fence_;

	Microsoft::WRL::ComPtr<IDxcUtils> dxcUtils_ = nullptr;
	Microsoft::WRL::ComPtr<IDxcCompiler3> dxcCompiler_ = nullptr;
	Microsoft::WRL::ComPtr<IDxcIncludeHandler> includeHandler_ = nullptr;

	// デルタタイム
	std::chrono::steady_clock::time_point lastFrameTime_;
	float deltaTime_ = 1.0f / 60.0f;
	bool useFixedFrameRate_ = true; // true: 固定, false: 可変

	// グローバルタイムスケール（0で停止、1で等速、0.5でスロー、2で倍速）
	float timeScale_ = 1.0f;

	HANDLE fenceEvent_ = nullptr;
	UINT frameIndex_ = 0;
	UINT rtvDescriptorSize_ = 0;
	uint64_t fenceValue_ = 0;

	// テクスチャ/バッファアップロード用の中間 UPLOAD バッファ。
	// 各エントリは「この値以下の fenceValue が完了したら解放してよい」というペア。
	std::vector<std::pair<uint64_t, Microsoft::WRL::ComPtr<ID3D12Resource>>> intermediateResources_;

	// fenceValue 完了時に呼ぶ汎用コールバック群。EnqueueOnFenceComplete で積み、
	// TickPendingCallbacks で完了済みのものを呼び出す。
	std::vector<std::pair<uint64_t, std::function<void()>>> pendingCallbacks_;
	WindowsApplication* winApp_ = nullptr;
	// スワップチェイン設定の記録用
	UINT bufferCount_ = 2;
	DXGI_FORMAT backBufferFormat_ = DXGI_FORMAT_R8G8B8A8_UNORM;

};
