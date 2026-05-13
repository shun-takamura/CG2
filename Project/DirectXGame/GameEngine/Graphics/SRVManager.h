#pragma once
#include"DirectXCore.h"
#include <wrl.h>
#include <cassert>
#include <queue>

// 前方宣言
class DirectXCore;

class SRVManager
{
	DirectXCore* dxCore_ = nullptr;

	// SRV用のディスクリプタサイズ
	uint32_t descriptorSize_;

	// SRV用のディスクリプタヒープ
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> descriptorHeap_;

	// 未使用スロットの先頭（一度も使っていない領域の先頭）
	uint32_t nextFresh_ = 0;

	// 解放済みスロットの再利用待ちキュー
	std::queue<uint32_t> freeList_;

public:

	// 最大SRV数(最大テクスチャ枚数)
	static const uint32_t kMaxSRVCount;

	// 初期化
	void Initialize(DirectXCore* dxCore);

	// 描画前処理
	void PreDraw();

	uint32_t Allocate();

	// スロットを返却する（テクスチャ破棄時に呼ぶ）
	void Free(uint32_t index);

	// 確保可能かチェック
	bool CanAllocate() const {
		return !freeList_.empty() || nextFresh_ < kMaxSRVCount;
	}

	// SRV生成(テクスチャ用)
	void CreateSRVForTexture2D(uint32_t srvIndex, ID3D12Resource* pResource, DXGI_FORMAT format, UINT MipLevels);

	/// <summary>
	/// Cubemap用のSRVを生成する
	/// </summary>
	void CreateSRVForCubemap(uint32_t srvIndex, ID3D12Resource* pResource, DXGI_FORMAT format, UINT MipLevels);

	// SRV生成(Structured Buffer用)
	void CreateSRVForStructuredBuffer(uint32_t srvIndex, ID3D12Resource* pResource, UINT elementNums, UINT structureByteStride);

	// UAV生成(Structured Buffer用)
	void CreateUAVForStructuredBuffer(uint32_t srvIndex, ID3D12Resource* pResource, UINT elementNums, UINT structureByteStride);

	// セッター
	void SetGraphicsRootDescriptorTable(UINT RootParameterIndex, uint32_t srvIndex);

	// ゲッター
	D3D12_CPU_DESCRIPTOR_HANDLE GetCPUDescriptorHandle(uint32_t index);
	D3D12_GPU_DESCRIPTOR_HANDLE GetGPUDescriptorHandle(uint32_t index);

};

