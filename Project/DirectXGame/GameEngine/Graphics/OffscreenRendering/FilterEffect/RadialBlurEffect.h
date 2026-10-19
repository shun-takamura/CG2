#pragma once
#include "BaseFilterEffect.h"

/// <summary>
/// 放射状ブラー（RadialBlur）エフェクト
/// 中心から外側へサンプリング点を伸ばし、その色を平均化する。
/// 深度のマスク（SetDepthThreshold）を付けると、それより手前の画素はぼかさない（一人称のコクピットの内装など）。
/// 深度を読むので outline 用ルートシグネチャ（color t0 + depth t1 + cbuffer b0）で作る（InitializeDepth）
/// </summary>
class RadialBlurEffect : public BaseFilterEffect
{
public:
	/// <summary>PostEffect から outline 用ルートシグネチャを受け取って初期化</summary>
	void InitializeDepth(
		DirectXCore* dxCore,
		ID3D12RootSignature* outlineRootSignature,
		const D3D12_GRAPHICS_PIPELINE_STATE_DESC& basePsoDesc
	);

	// Base 経由でも呼べるようダミー実装（未使用。InitializeDepth を使う）
	void Initialize(
		DirectXCore* /*dxCore*/,
		ID3D12RootSignature* /*copyRootSignature*/,
		ID3D12RootSignature* /*effectRootSignature*/,
		const D3D12_GRAPHICS_PIPELINE_STATE_DESC& /*basePsoDesc*/
	) override {}

	void UpdateConstantBuffer() override;
	void ShowImGui() override;
	void ResetParams() override;

	std::string GetName() const override { return "RadialBlur"; }
	bool NeedsCBuffer() const override { return true; }
	bool NeedsDepth() const override { return true; }

	// ===== 専用パラメータ設定 =====
	void SetCenter(float x, float y);
	void SetBlurWidth(float width);
	void SetNumSamples(int samples);
	/// <summary>深度（0..1、手前が 0）がこれ未満の画素はぼかさない。0 で無効（画面全体をぼかす）</summary>
	void SetDepthThreshold(float threshold) { depthThreshold_ = threshold; }
	/// <summary>カメラの near / far と目からの距離 [m] から、SetDepthThreshold に渡す深度を出す（左手系の透視投影）</summary>
	static float DepthAtDistance(float distance, float nearClip, float farClip);

	const float* GetCenter() const { return centerArray_; }
	float GetBlurWidth() const { return blurWidth_; }
	int GetNumSamples() const { return numSamples_; }

private:
	struct RadialBlurParamsCB
	{
		float center[2];   // 8バイト
		float blurWidth;   // 4
		int numSamples;    // 4 → 16バイト
		float depthThreshold;  // 4
		float _padding[3];     // → 合計32バイト
	};

	float centerArray_[2] = { 0.5f, 0.5f };
	float blurWidth_ = 0.01f;
	int numSamples_ = 10;
	float depthThreshold_ = 0.0f;
};
