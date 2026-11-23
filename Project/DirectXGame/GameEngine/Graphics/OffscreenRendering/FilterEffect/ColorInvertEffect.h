#pragma once
#include "BaseFilterEffect.h"

class ColorInvertEffect : public BaseFilterEffect
{
public:
	/// <summary>ColorInvert.PS の PSO を effect 用ルートシグネチャ（t0＋b0）で作り、定数バッファを作る</summary>
	/// <param name="dxCore">DirectXCoreへのポインタ</param>
	/// <param name="copyRootSignature">未使用</param>
	/// <param name="effectRootSignature">PSO に使う cbuffer あり用ルートシグネチャ</param>
	/// <param name="basePsoDesc">共通のパイプラインステート設定</param>
	void Initialize(
		DirectXCore* dxCore,
		ID3D12RootSignature* copyRootSignature,
		ID3D12RootSignature* effectRootSignature,
		const D3D12_GRAPHICS_PIPELINE_STATE_DESC& basePsoDesc
	) override;

	/// <summary>反転の強さを定数バッファへ書き込む</summary>
	void UpdateConstantBuffer() override;
	/// <summary>反転の強さを ImGui で編集する</summary>
	void ShowImGui() override;
	/// <summary>反転の強さを 1 に戻す</summary>
	void ResetParams() override;

	/// <summary>エフェクト名を返す</summary>
	/// <returns>"ColorInvert"</returns>
	std::string GetName() const override { return "ColorInvert"; }
	/// <summary>定数バッファを使う</summary>
	/// <returns>常に true</returns>
	bool NeedsCBuffer() const override { return true; }

	/// <summary>反転の強さを設定する（0〜1 に切り詰める）</summary>
	/// <param name="intensity">強さ（0=元の色、1=完全に反転）</param>
	void SetIntensity(float intensity);
	/// <summary>反転の強さを返す</summary>
	/// <returns>強さ（0〜1）</returns>
	float GetIntensity() const { return intensity_; }

private:
	struct ColorInvertParamsCB
	{
		float intensity = 1.0f;
		float _padding[3];
	};

	float intensity_ = 1.0f;
};
