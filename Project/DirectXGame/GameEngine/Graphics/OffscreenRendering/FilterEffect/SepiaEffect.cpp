#include "SepiaEffect.h"
#include "DirectXCore.h"
#include <cassert>
#include <cstring>
#include <algorithm>

#ifdef USE_IMGUI
#include "imgui.h"
#endif

void SepiaEffect::Initialize(DirectXCore* dxCore, ID3D12RootSignature* copyRootSignature, ID3D12RootSignature* effectRootSignature, const D3D12_GRAPHICS_PIPELINE_STATE_DESC& basePsoDesc)
{
    // シェーダーコンパイル
    IDxcBlob* psBlob = dxCore->CompileShader(
        L"Resources/Shaders/Sepia.PS.hlsl",
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
    CreateConstantBuffer(dxCore, sizeof(SepiaParamsCB));
    UpdateConstantBuffer();
}

void SepiaEffect::UpdateConstantBuffer()
{
    if (!constantBufferMappedPtr_) return;

    SepiaParamsCB cb;
    cb.intensity = intensity_;
    cb.sepiaColorR = sepiaColorR_;
    cb.sepiaColorG = sepiaColorG_;
    cb.sepiaColorB = sepiaColorB_;
    memcpy(constantBufferMappedPtr_, &cb, sizeof(cb));
}

void SepiaEffect::ShowImGui()
{
#ifdef USE_IMGUI
    ImGui::SliderFloat("Intensity##Sepia", &intensity_, 0.0f, 1.0f);
    ImGui::ColorEdit3("SepiaColor##Sepia", &sepiaColorR_);
#endif
}

void SepiaEffect::ResetParams()
{
    intensity_ = 1.0f;
    sepiaColorR_ = 1.0f;
    sepiaColorG_ = 0.691f;
    sepiaColorB_ = 0.402f;
}

void  SepiaEffect::SetSepiaColor(float r, float g, float b) {
    sepiaColorR_ = r;
    sepiaColorG_ = g;
    sepiaColorB_ = b;
}

void SepiaEffect::SetIntensity(float intensity) {
    intensity_ = std::clamp(intensity, 0.0f, 1.0f);
}