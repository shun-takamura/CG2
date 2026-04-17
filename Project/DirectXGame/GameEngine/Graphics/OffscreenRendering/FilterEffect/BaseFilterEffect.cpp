#include "BaseFilterEffect.h"
#include "DirectXCore.h"
#include <cassert>

void BaseFilterEffect::CreateConstantBuffer(DirectXCore* dxCore, uint32_t dataSize)
{
	// 256バイトアライメント
	uint32_t bufferSize = (dataSize + 0xFF) & ~0xFF;

	D3D12_HEAP_PROPERTIES heapProps{};
	heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

	D3D12_RESOURCE_DESC resourceDesc{};
	resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	resourceDesc.Width = bufferSize;
	resourceDesc.Height = 1;
	resourceDesc.DepthOrArraySize = 1;
	resourceDesc.MipLevels = 1;
	resourceDesc.Format = DXGI_FORMAT_UNKNOWN;
	resourceDesc.SampleDesc.Count = 1;
	resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

	HRESULT hr = dxCore->GetDevice()->CreateCommittedResource(
		&heapProps,
		D3D12_HEAP_FLAG_NONE,
		&resourceDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&constantBuffer_)
	);
	assert(SUCCEEDED(hr));

	hr = constantBuffer_->Map(0, nullptr, &constantBufferMappedPtr_);
	assert(SUCCEEDED(hr));
}
