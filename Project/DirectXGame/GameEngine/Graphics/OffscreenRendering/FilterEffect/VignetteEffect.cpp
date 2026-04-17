#include "VignetteEffect.h"
#include "DirectXCore.h"
#include <cassert>
#include <cstring>
#include <algorithm>

#ifdef USE_IMGUI
#include "imgui.h"
#endif

void VignetteEffect::Initialize(
    DirectXCore* dxCore,
    ID3D12RootSignature* copyRootSignature,
    ID3D12RootSignature* effectRootSignature,
    const D3D12_GRAPHICS_PIPELINE_STATE_DESC& basePsoDesc
) {
    // シェーダーコンパイル
    IDxcBlob* psBlob = dxCore->CompileShader(
        L"Resources/Shaders/Vignette.PS.hlsl",
        L"ps_6_0"
    );
    assert(psBlob);

    // パイプライン作成
    // cbufferありなら effectRootSignature、なしなら copyRootSignature
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = basePsoDesc;
    psoDesc.pRootSignature = effectRootSignature;
    psoDesc.PS = { psBlob->GetBufferPointer(), psBlob->GetBufferSize() };

    HRESULT hr = dxCore->GetDevice()->CreateGraphicsPipelineState(
        &psoDesc, IID_PPV_ARGS(&pipelineState_));
    assert(SUCCEEDED(hr));

    // 定数バッファ作成（基底クラスのヘルパー）
    // cbufferなしの場合はこの2行は不要
    CreateConstantBuffer(dxCore, sizeof(VignetteParamsCB));
    UpdateConstantBuffer();
}

void VignetteEffect::UpdateConstantBuffer() {
    if (!constantBufferMappedPtr_) return;

    VignetteParamsCB cb;
    cb.intensity = intensity_;
    cb.power = power_;
    cb.scale = scale_;
  
    memcpy(constantBufferMappedPtr_, &cb, sizeof(cb));
}

void VignetteEffect::ShowImGui() {
#ifdef USE_IMGUI
    ImGui::SliderFloat("Intensity##Vignette", &intensity_, 0.0f, 2.0f);
    ImGui::SliderFloat("Power##Vignette", &power_, 0.1f, 3.0f);
    ImGui::SliderFloat("Scale##Vignette", &scale_, 1.0f, 50.0f);
#endif
}

void VignetteEffect::ResetParams() {
    intensity_ = 0.5f;
    power_ = 0.8f;
    scale_ = 16.0f;
}

void VignetteEffect::SetIntensity(float intensity) {
    intensity_ = std::clamp(intensity, 0.0f, 1.0f);
}

void VignetteEffect::SetPower(float power)
{
    power_= std::clamp(power, 0.1f, 3.0f);
}

void VignetteEffect::SetScale(float scale)
{
    scale_ = std::clamp(scale, 1.0f, 50.0f);
}
