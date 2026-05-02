#pragma once
#include "DirectXCore.h"
#include <wrl.h>
#include <d3d12.h>
#include <dxcapi.h>
#include "Log.h"
#include <cassert>

// ComputeShaderでSkinningを行うためのRootSignature/PipelineStateを管理する
class SkinningComputeManager
{
private:
    DirectXCore* dxCore_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_ = nullptr;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState_ = nullptr;

    void CreateRootSignature();
    void CreateComputePipelineState();

public:
    void Initialize(DirectXCore* dxCore);
    ~SkinningComputeManager();

    void Release() {
        rootSignature_.Reset();
        pipelineState_.Reset();
    }

    // CS実行のためのCommandList設定
    void DispatchSetting();

    ID3D12RootSignature* GetRootSignature() const { return rootSignature_.Get(); }
    ID3D12PipelineState* GetPipelineState() const { return pipelineState_.Get(); }

    DirectXCore* GetDxCore() const { return dxCore_; }
};
