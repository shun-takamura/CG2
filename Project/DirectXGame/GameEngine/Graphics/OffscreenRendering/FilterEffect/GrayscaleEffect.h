#pragma once
#include "BaseFilterEffect.h"

/// <summary>
/// グレースケールエフェクト
/// 画面を白黒にする（BT.709方式）
/// intensityで元の色とのブレンド具合を調整可能
/// </summary>
class GrayscaleEffect : public BaseFilterEffect
{
public:
	/// <summary>Grayscale.PS の PSO を effect 用ルートシグネチャ（t0＋b0）で作り、定数バッファを作って初期値を書き込む</summary>
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

	/// <summary>強度を定数バッファへ書き込む</summary>
	void UpdateConstantBuffer() override;
	/// <summary>強度を ImGui で編集する</summary>
	void ShowImGui() override;
	/// <summary>強度を 1 に戻す</summary>
	void ResetParams() override;

	/// <summary>エフェクト名を返す</summary>
	/// <returns>"Grayscale"</returns>
	std::string GetName() const override { return "Grayscale"; }
	/// <summary>定数バッファを使う</summary>
	/// <returns>常に true</returns>
	bool NeedsCBuffer() const override { return true; }

	// ===== 専用パラメータ設定 =====

	/// <summary>
	/// 強度を設定（0.0 = 元の色, 1.0 = 完全な白黒）
	/// </summary>
	/// <param name="intensity">強度（0〜1 に切り詰める）</param>
	void SetIntensity(float intensity);

	/// <summary>強度を返す</summary>
	/// <returns>強度（0〜1）</returns>
	float GetIntensity() const { return intensity_; }

private:
	// cbuffer構造体（シェーダーと同じレイアウト）
	struct GrayscaleParamsCB
	{
		float intensity = 1.0f;  // offset: 0
		float _padding[3];       // offset: 4-15（16バイトアライメント）
	};

	float intensity_ = 1.0f;
};
