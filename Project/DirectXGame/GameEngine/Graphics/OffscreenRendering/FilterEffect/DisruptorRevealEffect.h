#pragma once
#include "BaseFilterEffect.h"

/// <summary>
/// ディスラプター「崩壊リビール＋色反転」エフェクト。
/// 断裂線（スクリーン UV の2端点）への垂直距離でマスクし、線から上下へ伝播する
/// リビール内は通常色・外は反転色（殻）に出し分ける。revealT を 0→1 で全画面反転→全画面通常。
/// マスクテクスチャは不要（線情報は cbuffer で持つ）ので ColorInvert と同じ effectRootSignature を使う。
/// </summary>
class DisruptorRevealEffect : public BaseFilterEffect
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

	std::string GetName() const override { return "DisruptorReveal"; }
	bool NeedsCBuffer() const override { return true; }

	// 断裂線の2端点（スクリーン UV, 0..1）。毎フレーム現在のカメラで射影して渡す。
	void SetLine(float p0x, float p0y, float p1x, float p1y) {
		lineP0_[0] = p0x; lineP0_[1] = p0y; lineP1_[0] = p1x; lineP1_[1] = p1y;
	}
	void SetRevealT(float t) { revealT_ = t; }
	void SetEdgeSoftness(float s) { edgeSoftness_ = s; }
	void SetIntensity(float v) { intensity_ = v; }
	void SetAspect(float a) { aspect_ = a; }

private:
	struct ParamsCB
	{
		float lineP0[2]{ 0.5f, 0.5f };
		float lineP1[2]{ 1.0f, 0.5f };
		float revealT = 0.0f;
		float edgeSoftness = 0.03f;
		float intensity = 1.0f;
		float aspect = 16.0f / 9.0f;
	};

	float lineP0_[2]{ 0.5f, 0.5f };
	float lineP1_[2]{ 1.0f, 0.5f };
	float revealT_ = 0.0f;
	float edgeSoftness_ = 0.03f;
	float intensity_ = 1.0f;
	float aspect_ = 16.0f / 9.0f;
};
