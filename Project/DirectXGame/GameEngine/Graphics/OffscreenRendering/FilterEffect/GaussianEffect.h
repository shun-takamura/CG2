#pragma once
#include "BaseFilterEffect.h"

/// <summary>
/// ガウシアンぼかしエフェクト
/// sigmaで広がり具合、kernelSizeでぼかし範囲を制御する
/// </summary>
class GaussianEffect : public BaseFilterEffect
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

	std::string GetName() const override { return "Gaussian"; }
	bool NeedsCBuffer() const override { return true; }

	// ===== 専用パラメータ設定 =====

	/// <summary>
	/// カーネルサイズを設定（奇数。偶数なら+1される）
	/// </summary>
	void SetKernelSize(int size);

	/// <summary>
	/// シグマを設定（ぼかしの広がり具合）
	/// </summary>
	void SetSigma(float sigma);

	/// <summary>
	/// カーネルサイズを取得
	/// </summary>
	int GetKernelSize() const { return kernelSize_; }

	/// <summary>
	/// シグマを取得
	/// </summary>
	float GetSigma() const { return sigma_; }

private:
	// cbuffer構造体（シェーダーと同じレイアウト）
	struct GaussianParamsCB
	{
		int kernelSize = 3;     // offset: 0
		float sigma = 1.0f;     // offset: 4
		float _padding[2];      // offset: 8-15（16バイトアライメント）
	};

	int kernelSize_ = 3;
	float sigma_ = 1.0f;
};
