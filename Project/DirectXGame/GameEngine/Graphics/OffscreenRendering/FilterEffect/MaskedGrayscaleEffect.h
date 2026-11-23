#pragma once
#include "BaseFilterEffect.h"

class RenderTexture;

/// <summary>
/// IDマスク参照グレースケールエフェクト。
/// idMaskRT（R8_UINT）の値が 0 のピクセルはグレースケール、非0 のピクセルは元色を維持する。
/// ジャスト回避演出など「特定のオブジェクトだけ色を残す」用途で使う。
/// </summary>
class MaskedGrayscaleEffect : public BaseFilterEffect
{
public:
	/// <summary>
	/// PostEffect から idMaskRT_ のポインタを受け取り、MaskedGrayscale.PS の PSO（outline 用ルートシグネチャを共用）と定数バッファを作る
	/// </summary>
	/// <param name="dxCore">DirectXCoreへのポインタ</param>
	/// <param name="outlineRootSignature">PSO に使う outline 用ルートシグネチャ（[0]=t0 色、[1]=t1 補助テクスチャ、[2]=b0、s0=linear、s1=point）</param>
	/// <param name="basePsoDesc">共通のパイプラインステート設定</param>
	/// <param name="idMaskRT">ID マスク RT（PostEffect 所有。t1 にバインドされる）</param>
	void InitializeMasked(
		DirectXCore* dxCore,
		ID3D12RootSignature* outlineRootSignature,
		const D3D12_GRAPHICS_PIPELINE_STATE_DESC& basePsoDesc,
		RenderTexture* idMaskRT
	);

	/// <summary>Base 経由でも呼べるようにしたダミー実装（未使用。初期化は専用の初期化関数）</summary>
	/// <param name="dxCore">未使用</param>
	/// <param name="copyRootSignature">未使用</param>
	/// <param name="effectRootSignature">未使用</param>
	/// <param name="basePsoDesc">未使用</param>
	void Initialize(
		[[maybe_unused]] DirectXCore* dxCore,
		[[maybe_unused]] ID3D12RootSignature* copyRootSignature,
		[[maybe_unused]] ID3D12RootSignature* effectRootSignature,
		[[maybe_unused]] const D3D12_GRAPHICS_PIPELINE_STATE_DESC& basePsoDesc
	) override {}

	/// <summary>強さを 0〜1 に切り詰めて定数バッファへ書き込む</summary>
	void UpdateConstantBuffer() override;
	/// <summary>強さを ImGui で編集する</summary>
	void ShowImGui() override;
	/// <summary>強さを 1 に戻す</summary>
	void ResetParams() override;

	/// <summary>エフェクト名を返す</summary>
	/// <returns>"MaskedGrayscale"</returns>
	std::string GetName() const override { return "MaskedGrayscale"; }
	/// <summary>定数バッファを使う</summary>
	/// <returns>常に true</returns>
	bool NeedsCBuffer() const override { return true; }
	/// <summary>ID マスク（t1）を使う</summary>
	/// <returns>常に true</returns>
	bool NeedsMaskTexture() const override { return true; }
	/// <summary>t1 にバインドする ID マスク RT の SRV インデックスを返す</summary>
	/// <returns>SRV インデックス。idMaskRT が無ければ 0</returns>
	uint32_t GetMaskTextureSRVIndex() const override;

	/// <summary>マスク外をグレースケールにする強さを設定する</summary>
	/// <param name="intensity">強さ（0=効果なし、1=完全なグレースケール）</param>
	void SetIntensity(float intensity) { intensity_ = intensity; }
	/// <summary>強さを返す</summary>
	/// <returns>強さ</returns>
	float GetIntensity() const { return intensity_; }

private:
	struct ParamsCB
	{
		float intensity = 1.0f;  // 0 = 効果無し, 1 = マスク外を完全グレースケール
		float _padding[3]{};
	};

	float intensity_ = 1.0f;
	RenderTexture* idMaskRT_ = nullptr;  // PostEffect 所有
};
