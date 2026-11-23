#pragma once
#include "BaseFilterEffect.h"
#include "Matrix4x4.h"

/// <summary>
/// Depth + Normal-from-depth 併用アウトラインエフェクト
/// 深度値の差からシルエットエッジを、深度から再構築した法線の差から折れ目エッジを取り、合成する
/// </summary>
class OutlineNormalEffect : public BaseFilterEffect
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
	/// Outline用ルートシグネチャを使って OutlineNormal.PS のパイプラインと定数バッファを作成
	/// </summary>
	/// <param name="dxCore">DirectXCoreへのポインタ</param>
	/// <param name="outlineRootSignature">PSO に使う outline 用ルートシグネチャ（[0]=t0 色、[1]=t1 深度、[2]=b0、s0=linear、s1=point）</param>
	/// <param name="basePsoDesc">共通のパイプラインステート設定</param>
	void InitializeOutline(
		DirectXCore* dxCore,
		ID3D12RootSignature* outlineRootSignature,
		const D3D12_GRAPHICS_PIPELINE_STATE_DESC& basePsoDesc
	);

	/// <summary>射影逆行列と重み・閾値を定数バッファへ書き込む</summary>
	void UpdateConstantBuffer() override;
	/// <summary>重み・閾値を ImGui で編集する</summary>
	void ShowImGui() override;
	/// <summary>重み・閾値を既定値に戻す</summary>
	void ResetParams() override;

	/// <summary>エフェクト名を返す</summary>
	/// <returns>"OutlineNormal"</returns>
	std::string GetName() const override { return "OutlineNormal"; }
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
	/// <summary>深度差のエッジの重みを設定する</summary>
	/// <param name="v">重み（0 未満は 0）</param>
	void SetDepthWeight(float v);
	/// <summary>法線差のエッジの重みを設定する</summary>
	/// <param name="v">重み（0 未満は 0）</param>
	void SetNormalWeight(float v);
	/// <summary>深度差のエッジとみなす閾値を設定する</summary>
	/// <param name="v">閾値（0〜1 に切り詰める）</param>
	void SetDepthThreshold(float v);
	/// <summary>法線差のエッジとみなす閾値を設定する</summary>
	/// <param name="v">閾値（0〜1 に切り詰める）</param>
	void SetNormalThreshold(float v);

private:
	struct OutlineNormalParamsCB
	{
		Matrix4x4 projectionInverse;  // 64
		float depthWeight;            // 4
		float normalWeight;           // 4
		float depthThreshold;         // 4
		float normalThreshold;        // 4 (合計80)
	};

	Matrix4x4 projectionInverse_{};
	float depthWeight_ = 6.0f;
	float normalWeight_ = 1.0f;
	float depthThreshold_ = 0.0f;
	float normalThreshold_ = 0.0f;
};
