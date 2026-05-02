#pragma once
#include "BaseFilterEffect.h"

/// <summary>
/// 放射状ブラー（RadialBlur）エフェクト
/// 中心から外側へサンプリング点を伸ばし、その色を平均化する
/// </summary>
class RadialBlurEffect : public BaseFilterEffect
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

	std::string GetName() const override { return "RadialBlur"; }
	bool NeedsCBuffer() const override { return true; }

	// ===== 専用パラメータ設定 =====
	void SetCenter(float x, float y);
	void SetBlurWidth(float width);
	void SetNumSamples(int samples);

	const float* GetCenter() const { return centerArray_; }
	float GetBlurWidth() const { return blurWidth_; }
	int GetNumSamples() const { return numSamples_; }

private:
	struct RadialBlurParamsCB
	{
		float center[2];   // 8バイト
		float blurWidth;   // 4
		int numSamples;    // 4 → 合計16バイト
	};

	float centerArray_[2] = { 0.5f, 0.5f };
	float blurWidth_ = 0.01f;
	int numSamples_ = 10;
};
