#pragma once
#include "BaseFilterEffect.h"
class SmoothingEffect :public BaseFilterEffect
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

	std::string GetName() const override { return "Smooting"; }
	bool NeedsCBuffer() const override { return true; }

	// ===== 専用パラメータ設定 =====

	/// <summary>
	/// カーネルサイズを設定（奇数。偶数なら+1される）
	/// </summary>
	void SetKernelSize(int size);

	/// <summary>
	/// カーネルサイズを取得
	/// </summary>
	int GetKernelSize() const { return kernelSize_; }

private:
	// cbuffer構造体（シェーダーと同じレイアウト）
	struct SmootingParamsCB
	{
		int kernelSize = 3;     // offset: 0
		float _padding[3];      // offset: 4-15
	};

	int kernelSize_ = 3;
	
};

