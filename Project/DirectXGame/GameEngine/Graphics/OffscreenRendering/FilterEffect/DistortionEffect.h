#pragma once
#include "BaseFilterEffect.h"
#include <cstdint>

class RenderTexture;

/// <summary>
/// ディストーション（背景歪み）ポストエフェクト。
/// DistortionRT に描画された歪みマップ（RG=方向, A=強度）を読み取り、
/// シーンテクスチャの UV をオフセットして合成する。
/// </summary>
class DistortionEffect : public BaseFilterEffect
{
public:
    /// <summary>
    /// PostEffect から distortionRT のポインタを受け取り、Distortion.PS の PSO と定数バッファを作る
    /// </summary>
    /// <param name="dxCore">DirectXCoreへのポインタ</param>
    /// <param name="outlineRootSignature">PSO に使うルートシグネチャ。PostEffect からは distortion 専用
    /// （[0]=t0 色、[1]=t1 歪みマップ、[2]=t2 深度、[3]=b0、s0=linear、s1=point）が渡される</param>
    /// <param name="basePsoDesc">共通のパイプラインステート設定</param>
    /// <param name="distortionRT">歪みマップ RT（PostEffect 所有。t1 にバインドされる）</param>
    void InitializeMasked(
        DirectXCore* dxCore,
        ID3D12RootSignature* outlineRootSignature,
        const D3D12_GRAPHICS_PIPELINE_STATE_DESC& basePsoDesc,
        RenderTexture* distortionRT
    );

    /// <summary>Base 経由でも呼べるようにしたダミー実装（未使用。初期化は InitializeMasked）</summary>
    /// <param name="dxCore">未使用</param>
    /// <param name="copyRootSignature">未使用</param>
    /// <param name="effectRootSignature">未使用</param>
    /// <param name="basePsoDesc">未使用</param>
    void Initialize(
        [[maybe_unused]] DirectXCore* dxCore,
        [[maybe_unused]] ID3D12RootSignature* copyRootSignature,
        [[maybe_unused]] ID3D12RootSignature* effectRootSignature,
        [[maybe_unused]] const D3D12_GRAPHICS_PIPELINE_STATE_DESC& basePsoDesc
    ) override {}

    /// <summary>歪みの強さ倍率を定数バッファへ書き込む</summary>
    void UpdateConstantBuffer() override;
    /// <summary>歪みの強さ倍率を ImGui で編集する</summary>
    void ShowImGui() override;
    /// <summary>歪みの強さ倍率を 0.1 に戻す</summary>
    void ResetParams() override;

    /// <summary>エフェクト名を返す</summary>
    /// <returns>"Distortion"</returns>
    std::string GetName() const override { return "Distortion"; }
    /// <summary>定数バッファを使う</summary>
    /// <returns>常に true</returns>
    bool NeedsCBuffer() const override { return true; }
    /// <summary>歪みマップ（t1）を使う</summary>
    /// <returns>常に true</returns>
    bool NeedsMaskTexture() const override { return true; }
    /// <summary>t1 にバインドする歪みマップ RT の SRV インデックスを返す</summary>
    /// <returns>SRV インデックス。distortionRT が無ければ 0</returns>
    uint32_t GetMaskTextureSRVIndex() const override;
    /// <summary>合成パスで scene depth を t2 から読むため、depth SRV 状態への遷移を要求する</summary>
    /// <returns>常に true</returns>
    bool NeedsDepth() const override { return true; }

    // ===== パラメータ =====

    /// <summary>歪みの強さ倍率を設定する</summary>
    /// <param name="s">倍率（PostEffect は 1 固定で使い、強さはインスタンスごとに歪みマップへ書く）</param>
    void SetStrength(float s) { strength_ = s; }
    /// <summary>歪みの強さ倍率を返す</summary>
    /// <returns>倍率</returns>
    float GetStrength() const { return strength_; }

private:
    // 定数バッファ構造体（シェーダーと同じレイアウト）
    struct DistortionParamsCB
    {
        float strength;     // 歪みの強さ倍率
        float _padding[3];  // 16byteアライメント
    };

    float strength_ = 0.1f;
    RenderTexture* distortionRT_ = nullptr; // PostEffect 所有
};
