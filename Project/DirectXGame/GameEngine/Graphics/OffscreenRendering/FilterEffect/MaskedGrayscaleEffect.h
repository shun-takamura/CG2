#pragma once
#include "BaseFilterEffect.h"

class RenderTexture;

/// <summary>
/// IDマスク参照グレースケールエフェクト。
/// idMaskRT（R8_UINT）の値が 0 のピクセルはグレースケール、非0 のピクセルは元色を維持する。
/// ジャスト回避演出など「特定のオブジェクトだけ色を残す」用途で使う。
/// </summary>
class MaskedGrayscaleEffect : public BaseFilterEffect
{
public:
	/// <summary>
	/// PostEffect から idMaskRT_ のポインタを受け取って初期化（outline 用ルートシグネチャを共用）
	/// </summary>
	void InitializeMasked(
		DirectXCore* dxCore,
		ID3D12RootSignature* outlineRootSignature,
		const D3D12_GRAPHICS_PIPELINE_STATE_DESC& basePsoDesc,
		RenderTexture* idMaskRT
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

	std::string GetName() const override { return "MaskedGrayscale"; }
	bool NeedsCBuffer() const override { return true; }
	bool NeedsMaskTexture() const override { return true; }
	uint32_t GetMaskTextureSRVIndex() const override;

	void SetIntensity(float intensity) { intensity_ = intensity; }
	float GetIntensity() const { return intensity_; }

private:
	struct ParamsCB
	{
		float intensity = 1.0f;  // 0 = 効果無し, 1 = マスク外を完全グレースケール
		float _padding[3]{};
	};

	float intensity_ = 1.0f;
	RenderTexture* idMaskRT_ = nullptr;  // PostEffect 所有
};
