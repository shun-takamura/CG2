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
    /// <summary>PrecisionBlur.PS の PSO を effect 用ルートシグネチャ（t0＋b0）で作り、定数バッファを作って初期値を書き込む</summary>
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

    /// <summary>強さ・くっきり半径・境目の幅・最大ぼかし・周辺減光を定数バッファへ書き込む</summary>
    void UpdateConstantBuffer() override;
    /// <summary>全パラメータを ImGui で編集する</summary>
    void ShowImGui() override;
    /// <summary>全パラメータを既定値に戻す</summary>
    void ResetParams() override;

    /// <summary>エフェクト名を返す</summary>
    /// <returns>"PrecisionBlur"</returns>
    std::string GetName() const override { return "PrecisionBlur"; }
    /// <summary>定数バッファを使う</summary>
    /// <returns>常に true</returns>
    bool NeedsCBuffer() const override { return true; }

    // ===== 専用パラメータ設定 =====
    /// <summary>効果全体の強さ（フェードイン/アウト用）を設定する</summary>
    /// <param name="intensity">強さ（0〜1 に切り詰める）</param>
    void SetIntensity(float intensity);
    /// <summary>効果全体の強さを返す</summary>
    /// <returns>強さ（0〜1）</returns>
    float GetIntensity() const { return intensity_; }

    /// <summary>ぼかさない中央の半径を設定する</summary>
    /// <param name="r">半径（中央からの正規化距離。角で 1。0〜1 に切り詰める）</param>
    void SetInnerRadius(float r);
    /// <summary>ぼかさない中央の半径を返す</summary>
    /// <returns>半径（中央からの正規化距離）</returns>
    float GetInnerRadius() const { return innerRadius_; }

    /// <summary>くっきりからぼけ最大までの境目の幅を設定する</summary>
    /// <param name="f">幅（正規化距離、0.01〜1 に切り詰める）</param>
    void SetFalloff(float f);
    /// <summary>境目の幅を返す</summary>
    /// <returns>幅（正規化距離）</returns>
    float GetFalloff() const { return falloff_; }

    /// <summary>周辺のぼかしの最大量を設定する</summary>
    /// <param name="px">最大ぼかし [px]（0〜64 に切り詰める）</param>
    void SetMaxBlurPx(float px);
    /// <summary>周辺のぼかしの最大量を返す</summary>
    /// <returns>最大ぼかし [px]</returns>
    float GetMaxBlurPx() const { return maxBlurPx_; }

    /// <summary>周辺減光の強さを設定する</summary>
    /// <param name="v">強さ（0〜1 に切り詰める）</param>
    void SetVignette(float v);
    /// <summary>周辺減光の強さを返す</summary>
    /// <returns>強さ（0〜1）</returns>
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
