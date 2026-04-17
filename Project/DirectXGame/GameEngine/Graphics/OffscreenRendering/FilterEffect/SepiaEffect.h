#pragma once
#include "BaseFilterEffect.h"
class SepiaEffect : public BaseFilterEffect
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

    std::string GetName() const override { return "Sepia"; }
    bool NeedsCBuffer() const override { return true; }  // cbufferなしならfalse

    // ===== 専用パラメータ設定 =====
    void SetIntensity(float intensity);
    float GetIntensity() const { return intensity_; }

    void SetSepiaColor(float r, float g, float b);

private:

    // cbuffer構造体（シェーダーと同じレイアウト）
    struct SepiaParamsCB
    {
        float intensity; // offset: 0
        float sepiaColorR; // offset: 4
        float sepiaColorG; // offset: 8
        float sepiaColorB; // offset: 12
    };

    float intensity_ = 1.0f;
    float sepiaColorR_ = 1.0f;
    float sepiaColorG_ = 0.691f;
    float sepiaColorB_ = 0.402f;
 
};