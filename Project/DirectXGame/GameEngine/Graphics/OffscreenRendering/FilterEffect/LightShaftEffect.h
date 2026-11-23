#pragma once
#include "BaseFilterEffect.h"
#include "Vector2.h"
#include "Vector3.h"

class RenderTexture;

/// <summary>
/// スクリーンスペースのライトシャフト（GPU Gems 3「Volumetric Light Scattering as a Post-Process」）。
/// idMaskRT（R8_UINT）で targetId の物だけを光源とみなし、各ピクセルから光源の中心へ向かって
/// マスクをサンプルして減衰をかけながら足し合わせ、加算する。放射状の光の筋と後光が1パスで出て、
/// 手前の物（扉板など）が光源を遮った所は自然に筋の影になる。
/// 画面全体の明るさで光らせる Bloom と違い、昼の空や白い物は光らない。
/// outline 用ルートシグネチャ（color t0 + aux t1 + cbuffer b0）を共用する。
/// </summary>
class LightShaftEffect : public BaseFilterEffect
{
public:
	/// <summary>LightShaft.PS の PSO を outline 用ルートシグネチャで作り、定数バッファを作る</summary>
	/// <param name="dxCore">DirectXCoreへのポインタ</param>
	/// <param name="outlineRootSignature">PSO に使う outline 用ルートシグネチャ（[0]=t0 色、[1]=t1 補助テクスチャ、[2]=b0、s0=linear、s1=point）</param>
	/// <param name="basePsoDesc">共通のパイプラインステート設定</param>
	/// <param name="idMaskRT">ID マスク RT（PostEffect 所有。t1 にバインドされ、targetId の所を光源とみなす）</param>
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

	/// <summary>光源の位置・色・強さ・サンプル設定を範囲内に切り詰めて定数バッファへ書き込む</summary>
	void UpdateConstantBuffer() override;
	/// <summary>光源の位置・色・強さ・サンプル設定・対象 ID を ImGui で編集する</summary>
	void ShowImGui() override;
	/// <summary>全パラメータを既定値に戻す</summary>
	void ResetParams() override;

	/// <summary>エフェクト名を返す</summary>
	/// <returns>"LightShaft"</returns>
	std::string GetName() const override { return "LightShaft"; }
	/// <summary>定数バッファを使う</summary>
	/// <returns>常に true</returns>
	bool NeedsCBuffer() const override { return true; }
	/// <summary>ID マスク（t1）を使う</summary>
	/// <returns>常に true</returns>
	bool NeedsMaskTexture() const override { return true; }
	/// <summary>t1 にバインドする ID マスク RT の SRV インデックスを返す</summary>
	/// <returns>SRV インデックス。idMaskRT が無ければ 0</returns>
	uint32_t GetMaskTextureSRVIndex() const override;

	// ===== パラメータ =====
	/// <summary>光源の中心を設定する</summary>
	/// <param name="uv">光源の中心（画面 UV）</param>
	void SetLightPosition(const Vector2& uv) { lightUV_ = uv; }
	/// <summary>光源とみなす idMask の値を設定する</summary>
	/// <param name="id">idMask の値</param>
	void SetTargetId(uint32_t id) { targetId_ = id; }
	/// <summary>光の色を設定する</summary>
	/// <param name="color">色（RGB）</param>
	void SetColor(const Vector3& color) { color_ = color; }
	/// <summary>全体の強さを設定する（0 で無効と同じ）</summary>
	/// <param name="v">強さ（0 未満は 0）</param>
	void SetIntensity(float v) { intensity_ = v; }
	/// <summary>光源へ向かってどこまでサンプルするかを設定する</summary>
	/// <param name="v">割合（1=光源の中心まで。0 未満は 0）</param>
	void SetDensity(float v) { density_ = v; }
	/// <summary>1 サンプルごとの減衰を設定する</summary>
	/// <param name="v">減衰率（0〜1）</param>
	void SetDecay(float v) { decay_ = v; }
	/// <summary>1 サンプルあたりの寄与を設定する</summary>
	/// <param name="v">寄与（0 未満は 0）</param>
	void SetWeight(float v) { weight_ = v; }
	/// <summary>光源へ向かうサンプル数を設定する</summary>
	/// <param name="n">サンプル数（1〜128 に切り詰める）</param>
	void SetSampleCount(uint32_t n) { sampleCount_ = n; }

private:
	struct ParamsCB
	{
		Vector2 lightUV;      // 8
		float density;        // 4
		float decay;          // 4（→16）
		Vector3 color;        // 12
		float weight;         // 4（→32）
		float intensity;      // 4
		uint32_t sampleCount; // 4
		uint32_t targetId;    // 4
		float padding;        // 4（→48）
	};

	Vector2 lightUV_{ 0.5f, 0.5f };
	Vector3 color_{ 1.0f, 0.92f, 0.78f };
	float intensity_ = 1.0f;
	float density_ = 0.9f;
	float decay_ = 0.97f;
	float weight_ = 0.04f;
	uint32_t sampleCount_ = 64;
	uint32_t targetId_ = 1;

	RenderTexture* idMaskRT_ = nullptr;  // PostEffect 所有
};
