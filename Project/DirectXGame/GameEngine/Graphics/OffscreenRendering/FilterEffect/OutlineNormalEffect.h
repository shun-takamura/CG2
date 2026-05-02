#pragma once
#include "BaseFilterEffect.h"
#include "Matrix4x4.h"

/// <summary>
/// Depth + Normal-from-depth 併用アウトラインエフェクト
/// 深度値の差からシルエットエッジを、深度から再構築した法線の差から折れ目エッジを取り、合成する
/// </summary>
class OutlineNormalEffect : public BaseFilterEffect
{
public:
	void Initialize(
		DirectXCore* /*dxCore*/,
		ID3D12RootSignature* /*copyRootSignature*/,
		ID3D12RootSignature* /*effectRootSignature*/,
		const D3D12_GRAPHICS_PIPELINE_STATE_DESC& /*basePsoDesc*/
	) override {}

	/// <summary>
	/// Outline用ルートシグネチャを使ってパイプラインを作成
	/// </summary>
	void InitializeOutline(
		DirectXCore* dxCore,
		ID3D12RootSignature* outlineRootSignature,
		const D3D12_GRAPHICS_PIPELINE_STATE_DESC& basePsoDesc
	);

	void UpdateConstantBuffer() override;
	void ShowImGui() override;
	void ResetParams() override;

	std::string GetName() const override { return "OutlineNormal"; }
	bool NeedsCBuffer() const override { return true; }
	bool NeedsDepth() const override { return true; }

	void SetProjectionMatrix(const Matrix4x4& projection) override;

	// ===== 専用パラメータ設定 =====
	void SetDepthWeight(float v);
	void SetNormalWeight(float v);
	void SetDepthThreshold(float v);
	void SetNormalThreshold(float v);

private:
	struct OutlineNormalParamsCB
	{
		Matrix4x4 projectionInverse;  // 64
		float depthWeight;            // 4
		float normalWeight;           // 4
		float depthThreshold;         // 4
		float normalThreshold;        // 4 (合計80)
	};

	Matrix4x4 projectionInverse_{};
	float depthWeight_ = 6.0f;
	float normalWeight_ = 1.0f;
	float depthThreshold_ = 0.0f;
	float normalThreshold_ = 0.0f;
};
