#include "PrecisionBlurEffect.h"
#include "DirectXCore.h"
#include <cassert>
#include <cstring>
#include <algorithm>

#ifdef USE_IMGUI
#include "imgui.h"
#endif

void PrecisionBlurEffect::Initialize(
    DirectXCore* dxCore,
    ID3D12RootSignature* copyRootSignature,
    ID3D12RootSignature* effectRootSignature,
    const D3D12_GRAPHICS_PIPELINE_STATE_DESC& basePsoDesc
) {
    IDxcBlob* psBlob = dxCore->CompileShader(
        L"Resources/Shaders/PostEffect/Filters/PrecisionBlur.PS.hlsl",
        L"ps_6_0"
    );
    assert(psBlob);

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = basePsoDesc;
    psoDesc.pRootSignature = effectRootSignature;
    psoDesc.PS = { psBlob->GetBufferPointer(), psBlob->GetBufferSize() };

    HRESULT hr = dxCore->GetDevice()->CreateGraphicsPipelineState(
        &psoDesc, IID_PPV_ARGS(&pipelineState_));
    assert(SUCCEEDED(hr));

    CreateConstantBuffer(dxCore, sizeof(PrecisionBlurParamsCB));
    UpdateConstantBuffer();
}

void PrecisionBlurEffect::UpdateConstantBuffer() {
    if (!constantBufferMappedPtr_) return;

    PrecisionBlurParamsCB cb;
    cb.intensity = intensity_;
    cb.innerRadius = innerRadius_;
    cb.falloff = falloff_;
    cb.maxBlurPx = maxBlurPx_;
    cb.vignette = vignette_;

    memcpy(constantBufferMappedPtr_, &cb, sizeof(cb));
}

void PrecisionBlurEffect::ShowImGui() {
#ifdef USE_IMGUI
    ImGui::SliderFloat("Intensity##PrecisionBlur", &intensity_, 0.0f, 1.0f);
    ImGui::SliderFloat("Inner Radius##PrecisionBlur", &innerRadius_, 0.0f, 1.0f);
    ImGui::SliderFloat("Falloff##PrecisionBlur", &falloff_, 0.01f, 1.0f);
    ImGui::SliderFloat("Max Blur (px)##PrecisionBlur", &maxBlurPx_, 0.0f, 32.0f);
    ImGui::SliderFloat("Vignette##PrecisionBlur", &vignette_, 0.0f, 1.0f);
#endif
}

void PrecisionBlurEffect::ResetParams() {
    intensity_ = 1.0f;
    innerRadius_ = 0.35f;
    falloff_ = 0.35f;
    maxBlurPx_ = 8.0f;
    vignette_ = 0.5f;
}

void PrecisionBlurEffect::SetIntensity(float intensity) {
    intensity_ = std::clamp(intensity, 0.0f, 1.0f);
}

void PrecisionBlurEffect::SetInnerRadius(float r) {
    innerRadius_ = std::clamp(r, 0.0f, 1.0f);
}

void PrecisionBlurEffect::SetFalloff(float f) {
    falloff_ = std::clamp(f, 0.01f, 1.0f);
}

void PrecisionBlurEffect::SetMaxBlurPx(float px) {
    maxBlurPx_ = std::clamp(px, 0.0f, 64.0f);
}

void PrecisionBlurEffect::SetVignette(float v) {
    vignette_ = std::clamp(v, 0.0f, 1.0f);
}
