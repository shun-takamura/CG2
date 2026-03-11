#pragma once

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

class ResourceObject {
public:
	ResourceObject(ID3D12Resource* resource)
		:resource_(resource)
	{
	}

	// ComPtrが自動解放するためデストラクタ不要
	~ResourceObject() = default;

	ID3D12Resource* Get() { return resource_.Get(); }

private:
	Microsoft::WRL::ComPtr<ID3D12Resource> resource_;
};