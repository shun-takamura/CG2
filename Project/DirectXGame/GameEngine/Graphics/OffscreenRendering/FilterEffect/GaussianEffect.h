#pragma once
#include "BaseFilterEffect.h"

/// <summary>
/// ガウシアンぼかしエフェクト
/// sigmaで広がり具合、kernelSizeでぼかし範囲を制御する
/// </summary>
class GaussianEffect : public BaseFilterEffect
{
public:
	/// <summary>GaussianFilter.PS の PSO を effect 用ルートシグネチャ（t0＋b0）で作り、定数バッファを作って初期値を書き込む</summary>
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

	/// <summary>カーネルサイズとシグマを定数バッファへ書き込む</summary>
	void UpdateConstantBuffer() override;
	/// <summary>カーネルサイズ（奇数に補正）とシグマを ImGui で編集する</summary>
	void ShowImGui() override;
	/// <summary>カーネルサイズ 3・シグマ 1 に戻す</summary>
	void ResetParams() override;

	/// <summary>エフェクト名を返す</summary>
	/// <returns>"Gaussian"</returns>
	std::string GetName() const override { return "Gaussian"; }
	/// <summary>定数バッファを使う</summary>
	/// <returns>常に true</returns>
	bool NeedsCBuffer() const override { return true; }

	// ===== 専用パラメータ設定 =====

	/// <summary>
	/// カーネルサイズを設定（奇数。偶数なら+1される）
	/// </summary>
	/// <param name="size">一辺のサンプル数 [px]（1 未満は 1）</param>
	void SetKernelSize(int size);

	/// <summary>
	/// シグマを設定（ぼかしの広がり具合）
	/// </summary>
	/// <param name="sigma">ガウス関数の標準偏差 [px]（0.1 未満は 0.1）</param>
	void SetSigma(float sigma);

	/// <summary>カーネルサイズを返す</summary>
	/// <returns>一辺のサンプル数 [px]（奇数）</returns>
	int GetKernelSize() const { return kernelSize_; }

	/// <summary>シグマを返す</summary>
	/// <returns>ガウス関数の標準偏差 [px]</returns>
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
