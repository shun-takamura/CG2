#pragma once
#include "BaseFilterEffect.h"
class SmoothingEffect :public BaseFilterEffect
{
public:
	/// <summary>Smoothing.PS の PSO を effect 用ルートシグネチャ（t0＋b0）で作り、定数バッファを作って初期値を書き込む</summary>
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

	/// <summary>カーネルサイズを定数バッファへ書き込む</summary>
	void UpdateConstantBuffer() override;
	/// <summary>カーネルサイズ（奇数に補正）を ImGui で編集する</summary>
	void ShowImGui() override;
	/// <summary>カーネルサイズを 3 に戻す</summary>
	void ResetParams() override;

	/// <summary>エフェクト名を返す</summary>
	/// <returns>"Smooting"</returns>
	std::string GetName() const override { return "Smooting"; }
	/// <summary>定数バッファを使う</summary>
	/// <returns>常に true</returns>
	bool NeedsCBuffer() const override { return true; }

	// ===== 専用パラメータ設定 =====

	/// <summary>
	/// カーネルサイズを設定（奇数。偶数なら+1される）
	/// </summary>
	/// <param name="size">平均を取る一辺のサンプル数 [px]（1 未満は 1）</param>
	void SetKernelSize(int size);

	/// <summary>カーネルサイズを返す</summary>
	/// <returns>一辺のサンプル数 [px]（奇数）</returns>
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

