#pragma once
#include "BaseFilterEffect.h"
#include "Matrix4x4.h"

/// <summary>
/// Depthベースのアウトラインエフェクト
/// シーンの深度バッファをPrewittフィルタで畳み込み、シルエットエッジを抽出する
/// </summary>
class OutlineDepthEffect : public BaseFilterEffect
{
public:
	// 通常のInitializeはダミー（outline用は別経路で初期化する）
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

	std::string GetName() const override { return "OutlineDepth"; }
	bool NeedsCBuffer() const override { return true; }
	bool NeedsDepth() const override { return true; }

	void SetProjectionMatrix(const Matrix4x4& projection) override;

	// ===== 専用パラメータ設定 =====
	void SetEdgeStrength(float v);
	float GetEdgeStrength() const { return edgeStrength_; }

private:
	// シェーダのcbufferと一致させる
	struct OutlineDepthParamsCB
	{
		Matrix4x4 projectionInverse;  // 64バイト
		float edgeStrength;           // 4
		float _padding[3];            // 12（16バイト境界揃え）
	};

	Matrix4x4 projectionInverse_{};
	float edgeStrength_ = 6.0f;
};
