#pragma once
#include "DirectXCore.h"
#include <wrl.h>
#include <d3d12.h>
#include <array>

class SRVManager;

class PrimitivePipeline {
public:
    // ブレンドモード（ParticleManagerと同じ体系）
    enum BlendMode {
        kBlendModeNone,
        kBlendModeNormal,
        kBlendModeAdd,
        kBlendModeSubtract,
        kBlendModeMultiply,
        kBlendModeScreen,
        kCountOfBlendMode
    };

    static PrimitivePipeline* GetInstance();

    void Initialize(DirectXCore* dxCore, SRVManager* srvManager);
    void Finalize();

    DirectXCore* GetDxCore() const { return dxCore_; }
    SRVManager* GetSRVManager() const { return srvManager_; }

    // 描画前の共通設定（RootSig / PSO / Topology をセット）
    void PreDraw(BlendMode blendMode, bool depthWrite = false);

    ID3D12RootSignature* GetRootSignature() const { return rootSignature_.Get(); }

private:
    PrimitivePipeline() = default;
    ~PrimitivePipeline() = default;
    PrimitivePipeline(const PrimitivePipeline&) = delete;
    PrimitivePipeline& operator=(const PrimitivePipeline&) = delete;

    void CreateRootSignature();
    void CreateGraphicsPipelineState(BlendMode mode, bool depthWrite);

    DirectXCore* dxCore_ = nullptr;
    SRVManager* srvManager_ = nullptr;

    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
    std::array<std::array<Microsoft::WRL::ComPtr<ID3D12PipelineState>, 2>, kCountOfBlendMode> pipelineStates_;
};