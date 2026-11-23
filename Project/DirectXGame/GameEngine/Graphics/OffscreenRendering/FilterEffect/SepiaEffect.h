#pragma once
#include "BaseFilterEffect.h"
class SepiaEffect : public BaseFilterEffect
{
public:
    /// <summary>Sepia.PS の PSO を effect 用ルートシグネチャ（t0＋b0）で作り、定数バッファを作って初期値を書き込む</summary>
    /// <param name="dxCore">DirectXCoreへのポインタ</param>
    /// <param name="copyRootSignature">未使用</param>
    /// <param name="effectRootSignature">PSO に使う cbuffer あり用ルートシグネチャ</param>
    /// <param name="basePsoDesc">共通のパイプラインステート設定</param>
    void Initialize(
        DirectXCore* dxCore,
        ID3D12RootSignature* copyRootSignature,
        ID3D12RootSignature* effectRootSignature,
        const D3D12_GRAPHICS_PIPELINE_STATE_DESC& basePsoDesc
    ) override;

    /// <summary>強さとセピアの色を定数バッファへ書き込む</summary>
    void UpdateConstantBuffer() override;
    /// <summary>強さとセピアの色を ImGui で編集する</summary>
    void ShowImGui() override;
    /// <summary>強さ 1・既定のセピア色に戻す</summary>
    void ResetParams() override;

    /// <summary>エフェクト名を返す</summary>
    /// <returns>"Sepia"</returns>
    std::string GetName() const override { return "Sepia"; }
    /// <summary>定数バッファを使う</summary>
    /// <returns>常に true</returns>
    bool NeedsCBuffer() const override { return true; }

    // ===== 専用パラメータ設定 =====
    /// <summary>強さを設定する</summary>
    /// <param name="intensity">強さ（0=元の色、1=完全なセピア。0〜1 に切り詰める）</param>
    void SetIntensity(float intensity);
    /// <summary>強さを返す</summary>
    /// <returns>強さ（0〜1）</returns>
    float GetIntensity() const { return intensity_; }

    /// <summary>輝度に掛けるセピアの色を設定する</summary>
    /// <param name="r">赤</param>
    /// <param name="g">緑</param>
    /// <param name="b">青</param>
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