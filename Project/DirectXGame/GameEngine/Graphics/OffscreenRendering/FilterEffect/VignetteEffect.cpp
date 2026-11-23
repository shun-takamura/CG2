#include "VignetteEffect.h"
#include "DirectXCore.h"
#include <cassert>
#include <cstring>
#include <algorithm>

#ifdef USE_IMGUI
#include "imgui.h"
#endif
namespace {
    // ResetParams で戻す既定値（強さ・暗くなり方の指数・広がりの倍率）
    constexpr float kDefaultIntensity = 0.5f;
    constexpr float kDefaultPower = 0.8f;
    constexpr float kDefaultScale = 16.0f;
    // SetPower で受け付ける指数の範囲
    constexpr float kMinPower = 0.1f;
    constexpr float kMaxPower = 3.0f;
    // SetScale で受け付ける倍率の上限（下限は 1）
    constexpr float kMaxScale = 50.0f;
}

void VignetteEffect::Initialize(
    DirectXCore* dxCore,
    ID3D12RootSignature* copyRootSignature,
    ID3D12RootSignature* effectRootSignature,
    const D3D12_GRAPHICS_PIPELINE_STATE_DESC& basePsoDesc
) {
    // シェーダーコンパイル
    IDxcBlob* psBlob = dxCore->LoadShaderBlob(
        L"Resources/Shaders/PostEffect/Filters/Vignette.PS.hlsl",
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
    intensity_ = kDefaultIntensity;
    power_ = kDefaultPower;
    scale_ = kDefaultScale;
}

void VignetteEffect::SetIntensity(float intensity) {
    intensity_ = std::clamp(intensity, 0.0f, 1.0f);
}

void VignetteEffect::SetPower(float power)
{
    power_= std::clamp(power, kMinPower, kMaxPower);
}

void VignetteEffect::SetScale(float scale)
{
    scale_ = std::clamp(scale, 1.0f, kMaxScale);
}
