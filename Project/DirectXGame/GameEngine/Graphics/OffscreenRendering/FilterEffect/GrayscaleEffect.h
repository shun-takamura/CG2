#pragma once
#include "BaseFilterEffect.h"

/// <summary>
/// グレースケールエフェクト
/// 画面を白黒にする（BT.709方式）
/// intensityで元の色とのブレンド具合を調整可能
/// </summary>
class GrayscaleEffect : public BaseFilterEffect
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

	std::string GetName() const override { return "Grayscale"; }
	bool NeedsCBuffer() const override { return true; }

	// ===== 専用パラメータ設定 =====

	/// <summary>
	/// 強度を設定（0.0 = 元の色, 1.0 = 完全な白黒）
	/// </summary>
	void SetIntensity(float intensity);

	/// <summary>
	/// 強度を取得
	/// </summary>
	float GetIntensity() const { return intensity_; }

private:
	// cbuffer構造体（シェーダーと同じレイアウト）
	struct GrayscaleParamsCB
	{
		float intensity = 1.0f;  // offset: 0
		float _padding[3];       // offset: 4-15（16バイトアライメント）
	};

	float intensity_ = 1.0f;
};
