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
    /// PostEffect から distortionRT のポインタを受け取って初期化（outline 用ルートシグネチャを共用）
    /// </summary>
    void InitializeMasked(
        DirectXCore* dxCore,
        ID3D12RootSignature* outlineRootSignature,
        const D3D12_GRAPHICS_PIPELINE_STATE_DESC& basePsoDesc,
        RenderTexture* distortionRT
    );

    // Base 経由でも呼べるようダミー実装（未使用）
    void Initialize(
        DirectXCore* /*dxCore*/,
        ID3D12RootSignature* /*copyRootSignature*/,
        ID3D12RootSignature* /*effectRootSignature*/,
        const D3D12_GRAPHICS_PIPELINE_STATE_DESC& /*basePsoDesc*/
    ) override {}

    void UpdateConstantBuffer() override;
    void ShowImGui() override;
    void ResetParams() override;

    std::string GetName() const override { return "Distortion"; }
    bool NeedsCBuffer() const override { return true; }
    bool NeedsMaskTexture() const override { return true; }
    uint32_t GetMaskTextureSRVIndex() const override;
    // 合成パスで scene depth を t2 から読むため、depth SRV 状態への遷移が必要
    bool NeedsDepth() const override { return true; }

    // ===== パラメータ =====

    void SetStrength(float s) { strength_ = s; }
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
