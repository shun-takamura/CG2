#pragma once
#include "BaseFilterEffect.h"

class VignetteEffect :public BaseFilterEffect
{
public:
    void Initialize(
        DirectXCore* dxCore,
        ID3D12RootSignature* copyRootSignature,
        ID3D12RootSignature* effectRootSignature,
        const D3D12_GRAPHICS_PIPELINE_STATE_DESC& basePsoDesc
    ) override;

    void UpdateConstantBuffer() override;
    void ShowImGui() override;
    void ResetParams() override;

    std::string GetName() const override { return "Vignette"; }
    bool NeedsCBuffer() const override { return true; }  // cbufferなしならfalse

    // ===== 専用パラメータ設定 =====
    void SetIntensity(float intensity);
    float GetIntensity() const { return intensity_; }

    void SetPower(float power);
    float GetPower() const { return power_; }

    void SetScale(float scale);
    float GetScale() const { return scale_; }

private:
    // cbuffer構造体（シェーダーと同じレイアウト）
    struct VignetteParamsCB
    {
        float intensity; // offset: 0
        float power; // offset: 4
        float scale; // offset: 8
        float _padding;
    };

    float intensity_ = 0.5f;
    float power_ = 0.8f;
    float scale_ = 16.0f;
};

