#pragma once
#include "BaseFilterEffect.h"
#include <string>

/// <summary>
/// Dissolve（ディゾルブ）エフェクト
/// マスクテクスチャの輝度が閾値以下のピクセルをdiscardし、
/// 閾値近傍にエッジ色を乗せる
/// </summary>
class DissolveEffect : public BaseFilterEffect
{
public:
	void Initialize(
		DirectXCore* /*dxCore*/,
		ID3D12RootSignature* /*copyRootSignature*/,
		ID3D12RootSignature* /*effectRootSignature*/,
		const D3D12_GRAPHICS_PIPELINE_STATE_DESC& /*basePsoDesc*/
	) override {}

	/// <summary>
	/// マスクテクスチャ対応のRootSignatureでパイプラインを作成
	/// </summary>
	void InitializeMasked(
		DirectXCore* dxCore,
		ID3D12RootSignature* maskedRootSignature,
		const D3D12_GRAPHICS_PIPELINE_STATE_DESC& basePsoDesc
	);

	void UpdateConstantBuffer() override;
	void ShowImGui() override;
	void ResetParams() override;

	std::string GetName() const override { return "Dissolve"; }
	bool NeedsCBuffer() const override { return true; }
	bool NeedsMaskTexture() const override { return true; }
	uint32_t GetMaskTextureSRVIndex() const override { return maskSrvIndex_; }

	// ===== 専用パラメータ =====
	void SetThreshold(float v);
	void SetEdgeWidth(float v);
	void SetEdgeColor(float r, float g, float b);
	void SetEdgeIntensity(float v);

	/// <summary>
	/// 抜き部分（mask <= threshold）の塗り色を設定（デフォルトは黒）
	/// </summary>
	void SetFillColor(float r, float g, float b);
	void SetFillIntensity(float v);

	/// <summary>
	/// マスクテクスチャを切り替える（事前にTextureManager::LoadTexture済みであること）
	/// 未ロードの場合は内部で自動的にロードする
	/// </summary>
	void SetMaskTexture(const std::string& texturePath);

	float GetThreshold() const { return threshold_; }

private:
	// マスク取得元の種類
	enum class MaskMode : int
	{
		Texture = 0,  // テクスチャから取得
		GPUNoise = 1, // Shaderで擬似乱数を生成
	};

	struct DissolveParamsCB
	{
		float threshold;     // 4
		float edgeWidth;     // 4
		float useNoise;      // 4 (Shader側はfloatで判定)
		float noiseScale;    // 4 → 16バイト
		float edgeColor[4];  // 16バイト（rgb + intensity）
		float fillColor[4];  // 16バイト（rgb + intensity）
		float noiseTime;     // 4
		float _pad[3];       // 12 → 16バイト
	};

	float threshold_ = 0.0f;
	float edgeWidth_ = 0.03f;
	float edgeColor_[3] = { 1.0f, 0.4f, 0.3f };
	float edgeIntensity_ = 1.0f;
	float fillColor_[3] = { 0.0f, 0.0f, 0.0f };
	float fillIntensity_ = 1.0f;

	MaskMode maskMode_ = MaskMode::Texture;
	float noiseScale_ = 16.0f;
	float noiseTime_ = 0.0f;

	std::string maskTexturePath_ = "Resources/Textures/MaskTexture/noise0.dds";
	uint32_t maskSrvIndex_ = 0;

	DirectXCore* dxCore_ = nullptr;
};
