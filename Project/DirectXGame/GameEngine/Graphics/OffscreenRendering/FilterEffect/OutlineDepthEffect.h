#pragma once
#include "BaseFilterEffect.h"
#include "Matrix4x4.h"

/// <summary>
/// Depthベースのアウトラインエフェクト
/// シーンの深度バッファをPrewittフィルタで畳み込み、シルエットエッジを抽出する
/// </summary>
class OutlineDepthEffect : public BaseFilterEffect
{
public:
	/// <summary>Base 経由でも呼べるようにしたダミー実装（未使用。初期化は InitializeOutline）</summary>
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

	/// <summary>
	/// Outline用ルートシグネチャを使って OutlineDepth.PS のパイプラインと定数バッファを作成
	/// </summary>
	/// <param name="dxCore">DirectXCoreへのポインタ</param>
	/// <param name="outlineRootSignature">PSO に使う outline 用ルートシグネチャ（[0]=t0 色、[1]=t1 深度、[2]=b0、s0=linear、s1=point）</param>
	/// <param name="basePsoDesc">共通のパイプラインステート設定</param>
	void InitializeOutline(
		DirectXCore* dxCore,
		ID3D12RootSignature* outlineRootSignature,
		const D3D12_GRAPHICS_PIPELINE_STATE_DESC& basePsoDesc
	);

	/// <summary>射影逆行列とエッジの強さを定数バッファへ書き込む</summary>
	void UpdateConstantBuffer() override;
	/// <summary>エッジの強さを ImGui で編集する</summary>
	void ShowImGui() override;
	/// <summary>エッジの強さを 6 に戻す</summary>
	void ResetParams() override;

	/// <summary>エフェクト名を返す</summary>
	/// <returns>"OutlineDepth"</returns>
	std::string GetName() const override { return "OutlineDepth"; }
	/// <summary>定数バッファを使う</summary>
	/// <returns>常に true</returns>
	bool NeedsCBuffer() const override { return true; }
	/// <summary>シーン深度（t1）を読むので、深度の遷移と射影行列の受け取りを要求する</summary>
	/// <returns>常に true</returns>
	bool NeedsDepth() const override { return true; }

	/// <summary>射影行列の逆行列を保持する（深度から視空間の位置を戻すのに使う）</summary>
	/// <param name="projection">カメラの射影行列</param>
	void SetProjectionMatrix(const Matrix4x4& projection) override;

	// ===== 専用パラメータ設定 =====
	/// <summary>エッジの強さを設定する</summary>
	/// <param name="v">強さ（0 未満は 0）</param>
	void SetEdgeStrength(float v);
	/// <summary>エッジの強さを返す</summary>
	/// <returns>強さ</returns>
	float GetEdgeStrength() const { return edgeStrength_; }

private:
	// シェーダのcbufferと一致させる
	struct OutlineDepthParamsCB
	{
		Matrix4x4 projectionInverse;  // 64バイト
		float edgeStrength;           // 4
		float _padding[3];            // 12（16バイト境界揃え）
	};

	Matrix4x4 projectionInverse_{};
	float edgeStrength_ = 6.0f;
};
