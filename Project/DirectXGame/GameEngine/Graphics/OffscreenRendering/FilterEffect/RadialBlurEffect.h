#pragma once
#include "BaseFilterEffect.h"

/// <summary>
/// 放射状ブラー（RadialBlur）エフェクト
/// 中心から外側へサンプリング点を伸ばし、その色を平均化する。
/// 深度のマスク（SetDepthThreshold）を付けると、それより手前の画素はぼかさない（一人称のコクピットの内装など）。
/// 深度を読むので outline 用ルートシグネチャ（color t0 + depth t1 + cbuffer b0）で作る（InitializeDepth）
/// </summary>
class RadialBlurEffect : public BaseFilterEffect
{
public:
	/// <summary>PostEffect から outline 用ルートシグネチャを受け取り、RadialBlur.PS の PSO と定数バッファを作る</summary>
	/// <param name="dxCore">DirectXCoreへのポインタ</param>
	/// <param name="outlineRootSignature">PSO に使う outline 用ルートシグネチャ（[0]=t0 色、[1]=t1 深度、[2]=b0、s0=linear、s1=point）</param>
	/// <param name="basePsoDesc">共通のパイプラインステート設定</param>
	void InitializeDepth(
		DirectXCore* dxCore,
		ID3D12RootSignature* outlineRootSignature,
		const D3D12_GRAPHICS_PIPELINE_STATE_DESC& basePsoDesc
	);

	/// <summary>Base 経由でも呼べるようにしたダミー実装（未使用。初期化は InitializeDepth）</summary>
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

	/// <summary>中心・ぼかし幅・サンプル数・深度のマスクを定数バッファへ書き込む</summary>
	void UpdateConstantBuffer() override;
	/// <summary>中心・ぼかし幅・サンプル数・深度のマスクを ImGui で編集する</summary>
	void ShowImGui() override;
	/// <summary>中心(0.5,0.5)・幅 0.01・10 サンプル・マスクなしに戻す</summary>
	void ResetParams() override;

	/// <summary>エフェクト名を返す</summary>
	/// <returns>"RadialBlur"</returns>
	std::string GetName() const override { return "RadialBlur"; }
	/// <summary>定数バッファを使う</summary>
	/// <returns>常に true</returns>
	bool NeedsCBuffer() const override { return true; }
	/// <summary>深度のマスクに t1 の深度を読むので、深度の遷移を要求する</summary>
	/// <returns>常に true</returns>
	bool NeedsDepth() const override { return true; }

	// ===== 専用パラメータ設定 =====
	/// <summary>放射の中心を設定する</summary>
	/// <param name="x">中心の X（画面 UV）</param>
	/// <param name="y">中心の Y（画面 UV）</param>
	void SetCenter(float x, float y);
	/// <summary>サンプル点を伸ばす幅を設定する</summary>
	/// <param name="width">1 サンプルあたりの伸び（UV、0 未満は 0）</param>
	void SetBlurWidth(float width);
	/// <summary>サンプル数を設定する</summary>
	/// <param name="samples">サンプル数（1〜32 に切り詰める）</param>
	void SetNumSamples(int samples);
	/// <summary>深度（0..1、手前が 0）がこれ未満の画素はぼかさない。0 で無効（画面全体をぼかす）</summary>
	/// <param name="threshold">深度の閾値（0〜1）</param>
	void SetDepthThreshold(float threshold) { depthThreshold_ = threshold; }
	/// <summary>カメラの near / far と目からの距離 [m] から、SetDepthThreshold に渡す深度を出す（左手系の透視投影）</summary>
	/// <param name="distance">目からの距離 [m]</param>
	/// <param name="nearClip">ニアクリップ [m]</param>
	/// <param name="farClip">ファークリップ [m]</param>
	/// <returns>深度（0〜1）。distance が near 以下、または far が near 以下なら 0</returns>
	static float DepthAtDistance(float distance, float nearClip, float farClip);

	/// <summary>放射の中心を返す</summary>
	/// <returns>要素 2 の配列（画面 UV の X, Y）</returns>
	const float* GetCenter() const { return centerArray_; }
	/// <summary>サンプル点を伸ばす幅を返す</summary>
	/// <returns>幅（UV）</returns>
	float GetBlurWidth() const { return blurWidth_; }
	/// <summary>サンプル数を返す</summary>
	/// <returns>サンプル数</returns>
	int GetNumSamples() const { return numSamples_; }

private:
	struct RadialBlurParamsCB
	{
		float center[2];   // 8バイト
		float blurWidth;   // 4
		int numSamples;    // 4 → 16バイト
		float depthThreshold;  // 4
		float _padding[3];     // → 合計32バイト
	};

	float centerArray_[2] = { 0.5f, 0.5f };
	float blurWidth_ = 0.01f;
	int numSamples_ = 10;
	float depthThreshold_ = 0.0f;
};
