#pragma once
#include "BaseFilterEffect.h"

class ColorInvertEffect : public BaseFilterEffect
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

	std::string GetName() const override { return "ColorInvert"; }
	bool NeedsCBuffer() const override { return true; }

	void SetIntensity(float intensity);
	float GetIntensity() const { return intensity_; }

private:
	struct ColorInvertParamsCB
	{
		float intensity = 1.0f;
		float _padding[3];
	};

	float intensity_ = 1.0f;
};
