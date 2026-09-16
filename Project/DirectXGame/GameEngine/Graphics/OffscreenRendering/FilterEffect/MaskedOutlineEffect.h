#pragma once
#include "BaseFilterEffect.h"
#include "Matrix4x4.h"
#include "Vector4.h"

class RenderTexture;

/// <summary>
/// IDマスクで指定したオブジェクトだけに、深度＋深度から再構築した法線ベースの
/// アウトラインを乗せるエフェクト（OutlineNormal のマスク版）。
///
/// idMaskRT（R8_UINT）の値でアウトライン色を切り替える：
///   0 = アウトライン無し
///   1 = colorFire_（炎状態）
///   2 = colorIce_ （氷状態）
/// blinkHz_ / minIntensity_ で点滅させる（time_ は毎フレーム外部から渡す）。
///
/// color(t0) + scene depth(t1) + idMask(t2) + cbuffer(b0) の専用ルートシグネチャを使う。
/// </summary>
class MaskedOutlineEffect : public BaseFilterEffect
{
public:
	/// <summary>
	/// 専用ルートシグネチャ（color t0 + depth t1 + idMask t2 + cbuffer b0）でパイプラインを作成。
	/// </summary>
	void InitializeMaskedOutline(
		DirectXCore* dxCore,
		ID3D12RootSignature* maskedOutlineRootSignature,
		const D3D12_GRAPHICS_PIPELINE_STATE_DESC& basePsoDesc,
		RenderTexture* idMaskRT
	);

	// 通常の Initialize はダミー（別経路で初期化する）
	void Initialize(
		DirectXCore* /*dxCore*/,
		ID3D12RootSignature* /*copyRootSignature*/,
		ID3D12RootSignature* /*effectRootSignature*/,
		const D3D12_GRAPHICS_PIPELINE_STATE_DESC& /*basePsoDesc*/
	) override {}

	void UpdateConstantBuffer() override;
	void ShowImGui() override;
	void ResetParams() override;

	std::string GetName() const override { return "MaskedOutline"; }
	bool NeedsCBuffer() const override { return true; }
	bool NeedsDepth() const override { return true; }        // 深度遷移＋射影行列伝播に乗せる
	bool NeedsMaskTexture() const override { return true; }  // idMaskRT のポインタ保持用
	uint32_t GetMaskTextureSRVIndex() const override;

	void SetProjectionMatrix(const Matrix4x4& projection) override;

	// ===== 専用パラメータ設定 =====
	void SetFireColor(const Vector4& c) { colorFire_ = c; }
	void SetIceColor(const Vector4& c) { colorIce_ = c; }
	void SetTime(float t) { time_ = t; }
	void SetBlinkHz(float v) { blinkHz_ = v; }
	void SetMinIntensity(float v) { minIntensity_ = v; }
	void SetDepthWeight(float v) { depthWeight_ = v; }
	void SetNormalWeight(float v) { normalWeight_ = v; }
	void SetDepthThreshold(float v) { depthThreshold_ = v; }
	void SetNormalThreshold(float v) { normalThreshold_ = v; }
	void SetEdgeStrength(float v) { edgeStrength_ = v; }

private:
	// シェーダの cbuffer と一致させる（128 バイト）
	struct MaskedOutlineParamsCB
	{
		Matrix4x4 projectionInverse;  // 64
		Vector4 colorFire;            // 16（id==1）
		Vector4 colorIce;             // 16（id==2）
		float depthWeight;            // 4
		float normalWeight;           // 4
		float depthThreshold;         // 4
		float normalThreshold;        // 4（→16）
		float edgeStrength;           // 4
		float time;                   // 4
		float blinkHz;                // 4
		float minIntensity;           // 4（→16）
	};

	Matrix4x4 projectionInverse_{};
	Vector4 colorFire_{ 1.0f, 0.0f, 0.0f, 1.0f };  // 0xFF0000FF
	Vector4 colorIce_{ 0.0f, 0.0f, 1.0f, 1.0f };   // 0x0000FFFF
	float depthWeight_ = 6.0f;
	float normalWeight_ = 1.0f;
	float depthThreshold_ = 0.0f;
	float normalThreshold_ = 0.0f;
	float edgeStrength_ = 1.5f;
	float time_ = 0.0f;
	float blinkHz_ = 3.0f;
	float minIntensity_ = 0.35f;

	RenderTexture* idMaskRT_ = nullptr;  // PostEffect 所有
};
