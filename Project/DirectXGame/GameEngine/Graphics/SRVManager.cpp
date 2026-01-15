#include "SRVManager.h"

const uint32_t SRVManager::kMaxSRVCount = 512;

uint32_t SRVManager::Allocate()
{
	// 上限に達していないかチェック
	//assert()

	// 返す番号を一旦記録
	int index = useIndex;

	// 次回のために番号をひとつ進める
	++useIndex;

	return index;
}

D3D12_CPU_DESCRIPTOR_HANDLE SRVManager::GetCPUDescriptorHandle(uint32_t index)
{
	D3D12_CPU_DESCRIPTOR_HANDLE CPUHandle = descriptorHeap_->GetCPUDescriptorHandleForHeapStart();
	CPUHandle.ptr += (descriptorSize_ * index);
	return CPUHandle;
}

D3D12_GPU_DESCRIPTOR_HANDLE SRVManager::GetGPUDescriptorHandle(uint32_t index)
{
	D3D12_GPU_DESCRIPTOR_HANDLE GPUHandle = descriptorHeap_->GetGPUDescriptorHandleForHeapStart();
	GPUHandle.ptr += (descriptorSize_ * index);
	return GPUHandle;
}

void SRVManager::CreateSRVForTexture2D(uint32_t srvIndex, ID3D12Resource* pResource, DXGI_FORMAT format, UINT MipLevels)
{
	// SRVの設定
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Format = format;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = MipLevels;

	// SRVの生成
	dxCore_->GetDevice()->CreateShaderResourceView(
		pResource,
		&srvDesc,
		GetCPUDescriptorHandle(srvIndex)
	);
}

void SRVManager::CreateSRVForStructuredBuffer(uint32_t srvIndex, ID3D12Resource* pResource, UINT elementNums, UINT structureByteStride)
{
	// SRVの設定
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Format = DXGI_FORMAT_UNKNOWN;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
	srvDesc.Buffer.FirstElement = 0;
	srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
	srvDesc.Buffer.NumElements = elementNums;
	srvDesc.Buffer.StructureByteStride = structureByteStride;

	// SRVの生成
	dxCore_->GetDevice()->CreateShaderResourceView(
		pResource,
		&srvDesc,
		GetCPUDescriptorHandle(srvIndex)
	);
}

void SRVManager::Initialize(DirectXCore* dxCore)
{
	dxCore_ = dxCore;

	// ディスクリプタヒープ生成
	descriptorHeap_ = dxCore_->CreateDescriptorHeap(
		dxCore_->GetDevice(),
		D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
		kMaxSRVCount, true
	);

	// ディスクリプタ1個分のサイズを取得して記録
	descriptorSize_ = dxCore_->GetDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

}

void SRVManager::PreDraw()
{
	// 描画前のDescriptorHeapの設定
	ID3D12DescriptorHeap* descriptorHeaps[] = { descriptorHeap_.Get() };
	dxCore_->GetCommandList()->SetDescriptorHeaps(1, descriptorHeaps);

}

void SRVManager::SetGraPhicsRootDescriptorTable(UINT RootParameterIndex, uint32_t srvIndex)
{
	dxCore_->GetCommandList()->SetGraphicsRootDescriptorTable(RootParameterIndex, GetGPUDescriptorHandle(srvIndex));
}
