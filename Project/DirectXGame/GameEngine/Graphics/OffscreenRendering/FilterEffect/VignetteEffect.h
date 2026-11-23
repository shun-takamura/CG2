#pragma once
#include "BaseFilterEffect.h"

class VignetteEffect :public BaseFilterEffect
{
public:
    /// <summary>Vignette.PS の PSO を effect 用ルートシグネチャ（t0＋b0）で作り、定数バッファを作って初期値を書き込む</summary>
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

    /// <summary>強さ・べき・スケールを定数バッファへ書き込む</summary>
    void UpdateConstantBuffer() override;
    /// <summary>強さ・べき・スケールを ImGui で編集する</summary>
    void ShowImGui() override;
    /// <summary>強さ 0.5・べき 0.8・スケール 16 に戻す</summary>
    void ResetParams() override;

    /// <summary>エフェクト名を返す</summary>
    /// <returns>"Vignette"</returns>
    std::string GetName() const override { return "Vignette"; }
    /// <summary>定数バッファを使う</summary>
    /// <returns>常に true</returns>
    bool NeedsCBuffer() const override { return true; }

    // ===== 専用パラメータ設定 =====
    /// <summary>周辺を暗くする強さを設定する</summary>
    /// <param name="intensity">強さ（0〜1 に切り詰める）</param>
    void SetIntensity(float intensity);
    /// <summary>周辺を暗くする強さを返す</summary>
    /// <returns>強さ</returns>
    float GetIntensity() const { return intensity_; }

    /// <summary>暗くなり方のカーブ（べき乗）を設定する</summary>
    /// <param name="power">べき（0.1〜3 に切り詰める）</param>
    void SetPower(float power);
    /// <summary>暗くなり方のカーブを返す</summary>
    /// <returns>べき</returns>
    float GetPower() const { return power_; }

    /// <summary>暗くならない中央の広さに効く倍率を設定する</summary>
    /// <param name="scale">倍率（1〜50 に切り詰める）</param>
    void SetScale(float scale);
    /// <summary>中央の広さに効く倍率を返す</summary>
    /// <returns>倍率</returns>
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

