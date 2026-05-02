#include "DistortionEffect.h"
#include "DirectXCore.h"
#include <cassert>
#include <cstring>

#ifdef USE_IMGUI
#include "imgui.h"
#endif

void DistortionEffect::Initialize(
    DirectXCore* dxCore,
    ID3D12RootSignature* copyRootSignature,
    ID3D12RootSignature* effectRootSignature,
    const D3D12_GRAPHICS_PIPELINE_STATE_DESC& basePsoDesc
) {
    // シェーダーコンパイル
    IDxcBlob* psBlob = dxCore->CompileShader(
        L"Resources/Shaders/PostEffect/Filters/Distortion.PS.hlsl",
        L"ps_6_0"
    );
    assert(psBlob);

    // パイプライン作成（cbufferあり → effectRootSignature）
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = basePsoDesc;
    psoDesc.pRootSignature = effectRootSignature;
    psoDesc.PS = { psBlob->GetBufferPointer(), psBlob->GetBufferSize() };

    HRESULT hr = dxCore->GetDevice()->CreateGraphicsPipelineState(
        &psoDesc, IID_PPV_ARGS(&pipelineState_));
    assert(SUCCEEDED(hr));

    // 定数バッファ作成
    CreateConstantBuffer(dxCore, sizeof(DistortionParamsCB));
    UpdateConstantBuffer();
}

void DistortionEffect::UpdateConstantBuffer() {
    if (!constantBufferMappedPtr_) return;

    DistortionParamsCB cb{};
    cb.strength = strength_;

    memcpy(constantBufferMappedPtr_, &cb, sizeof(cb));
}

void DistortionEffect::ShowImGui() {
#ifdef USE_IMGUI
    ImGui::SliderFloat("Strength##Distortion", &strength_, 0.0f, 1.0f);
#endif
}

void DistortionEffect::ResetParams() {
    strength_ = 0.1f;
}
