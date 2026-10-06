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
	void InitializeMasked(
		DirectXCore* dxCore,
		ID3D12RootSignature* outlineRootSignature,
		const D3D12_GRAPHICS_PIPELINE_STATE_DESC& basePsoDesc,
		RenderTexture* idMaskRT
	);

	// Base 経由でも呼べるようダミー実装（未使用）
	void Initialize(
		DirectXCore* /*dxCore*/,
		ID3D12RootSignature* /*copyRootSignature*/,
		ID3D12RootSignature* /*effectRootSignature*/,
		const D3D12_GRAPHICS_PIPELINE_STATE_DESC& /*basePsoDesc*/
	) override {}

	void UpdateConstantBuffer() override;
	void ShowImGui() override;
	void ResetParams() override;

	std::string GetName() const override { return "LightShaft"; }
	bool NeedsCBuffer() const override { return true; }
	bool NeedsMaskTexture() const override { return true; }
	uint32_t GetMaskTextureSRVIndex() const override;

	// ===== パラメータ =====
	void SetLightPosition(const Vector2& uv) { lightUV_ = uv; }   // 光源の中心（画面 UV）
	void SetTargetId(uint32_t id) { targetId_ = id; }           // 光源とみなす idMask の値
	void SetColor(const Vector3& color) { color_ = color; }
	void SetIntensity(float v) { intensity_ = v; }               // 全体の強さ（0 で無効と同じ）
	void SetDensity(float v) { density_ = v; }                   // 光源へ向かってどこまでサンプルするか（1=光源の中心まで）
	void SetDecay(float v) { decay_ = v; }                       // 1サンプルごとの減衰
	void SetWeight(float v) { weight_ = v; }                     // 1サンプルあたりの寄与
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
