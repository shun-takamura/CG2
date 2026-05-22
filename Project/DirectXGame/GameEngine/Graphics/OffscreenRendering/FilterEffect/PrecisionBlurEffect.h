#pragma once
#include "BaseFilterEffect.h"

/// <summary>
/// 精密射撃モード用の周辺ぼかしエフェクト。
/// 画面中央(innerRadius 内)はくっきり、周辺ほどガウスぼかしが強くなる。
/// intensity でフェードイン/アウトを制御する（maskedGrayscale と同じ運用）。
/// </summary>
class PrecisionBlurEffect : public BaseFilterEffect
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

    std::string GetName() const override { return "PrecisionBlur"; }
    bool NeedsCBuffer() const override { return true; }

    // ===== 専用パラメータ設定 =====
    void SetIntensity(float intensity);
    float GetIntensity() const { return intensity_; }

    void SetInnerRadius(float r);
    float GetInnerRadius() const { return innerRadius_; }

    void SetFalloff(float f);
    float GetFalloff() const { return falloff_; }

    void SetMaxBlurPx(float px);
    float GetMaxBlurPx() const { return maxBlurPx_; }

    void SetVignette(float v);
    float GetVignette() const { return vignette_; }

private:
    // cbuffer 構造体（シェーダーと同じレイアウト、32バイト）
    struct PrecisionBlurParamsCB
    {
        float intensity;   // offset: 0
        float innerRadius; // offset: 4
        float falloff;     // offset: 8
        float maxBlurPx;   // offset: 12
        float vignette;    // offset: 16
        float _padding[3]; // offset: 20-31
    };

    float intensity_ = 1.0f;
    float innerRadius_ = 0.35f;
    float falloff_ = 0.35f;
    float maxBlurPx_ = 8.0f;
    float vignette_ = 0.5f;
};
