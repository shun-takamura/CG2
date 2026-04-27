#pragma once
#include "BaseFilterEffect.h"
#include <cstdint>

/// <summary>
/// ディストーション（背景歪み）ポストエフェクト
/// ディストーションRTに描画された歪みマップを読み取り、シーンのUVをオフセットする
/// </summary>
class DistortionEffect : public BaseFilterEffect
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

    std::string GetName() const override { return "Distortion"; }
    bool NeedsCBuffer() const override { return true; }

    // ===== ディストーションRT連携 =====

    /// <summary>
    /// ディストーションRTのSRVインデックスを設定
    /// PostEffectが初期化時に呼ぶ
    /// </summary>
    void SetDistortionSRVIndex(uint32_t index) { distortionSrvIndex_ = index; }
    uint32_t GetDistortionSRVIndex() const { return distortionSrvIndex_; }

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
    uint32_t distortionSrvIndex_ = 0;
};
