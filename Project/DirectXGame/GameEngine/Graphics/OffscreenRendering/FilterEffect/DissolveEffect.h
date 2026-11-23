#pragma once
#include "BaseFilterEffect.h"
#include <string>

/// <summary>
/// Dissolve（ディゾルブ）エフェクト
/// マスクテクスチャの輝度が閾値以下のピクセルをdiscardし、
/// 閾値近傍にエッジ色を乗せる
/// </summary>
class DissolveEffect : public BaseFilterEffect
{
public:
	/// <summary>Base 経由でも呼べるようにしたダミー実装（未使用。初期化は InitializeMasked）</summary>
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
	/// マスクテクスチャ対応のルートシグネチャ（t0=色、t1=マスク、b0）で Dissolve.PS の PSO を作り、
	/// 定数バッファを作って既定のマスク（noise0.dds、線形）を読み込む
	/// </summary>
	/// <param name="dxCore">DirectXCoreへのポインタ</param>
	/// <param name="maskedRootSignature">outline 用ルートシグネチャ（t0＋t1＋b0）</param>
	/// <param name="basePsoDesc">共通のパイプラインステート設定</param>
	void InitializeMasked(
		DirectXCore* dxCore,
		ID3D12RootSignature* maskedRootSignature,
		const D3D12_GRAPHICS_PIPELINE_STATE_DESC& basePsoDesc
	);

	/// <summary>閾値・縁・塗り・ノイズの設定を定数バッファへ書き込む。GPU ノイズのときはノイズの時間も進める</summary>
	void UpdateConstantBuffer() override;
	/// <summary>閾値・縁・塗り・マスクの取得元を ImGui で編集する</summary>
	void ShowImGui() override;
	/// <summary>全パラメータを既定値に戻し、マスクの取得元をテクスチャに戻す（マスクのテクスチャ自体は変えない）</summary>
	void ResetParams() override;

	/// <summary>エフェクト名を返す</summary>
	/// <returns>"Dissolve"</returns>
	std::string GetName() const override { return "Dissolve"; }
	/// <summary>定数バッファを使う</summary>
	/// <returns>常に true</returns>
	bool NeedsCBuffer() const override { return true; }
	/// <summary>マスクテクスチャ（t1）を使う</summary>
	/// <returns>常に true</returns>
	bool NeedsMaskTexture() const override { return true; }
	/// <summary>t1 にバインドするマスクテクスチャの SRV インデックスを返す</summary>
	/// <returns>SRV インデックス</returns>
	uint32_t GetMaskTextureSRVIndex() const override { return maskSrvIndex_; }

	// ===== 専用パラメータ =====
	/// <summary>抜く閾値を設定する（マスクの値がこれ以下のピクセルを抜く）</summary>
	/// <param name="v">閾値（0〜1 に切り詰める）</param>
	void SetThreshold(float v);
	/// <summary>閾値の近くに乗せる縁の幅を設定する</summary>
	/// <param name="v">マスク値での幅（0 未満は 0）</param>
	void SetEdgeWidth(float v);
	/// <summary>縁の色を設定する</summary>
	/// <param name="r">赤</param>
	/// <param name="g">緑</param>
	/// <param name="b">青</param>
	void SetEdgeColor(float r, float g, float b);
	/// <summary>縁の色の強さを設定する</summary>
	/// <param name="v">強さ（0 未満は 0）</param>
	void SetEdgeIntensity(float v);

	/// <summary>
	/// 抜き部分（mask <= threshold）の塗り色を設定（デフォルトは黒）
	/// </summary>
	/// <param name="r">赤</param>
	/// <param name="g">緑</param>
	/// <param name="b">青</param>
	void SetFillColor(float r, float g, float b);
	/// <summary>抜き部分の塗り色の強さを設定する</summary>
	/// <param name="v">強さ（0 未満は 0）</param>
	void SetFillIntensity(float v);

	/// <summary>
	/// マスクテクスチャを切り替える。未ロードの場合は内部で線形（sRGB 変換なし）としてロードする
	/// </summary>
	/// <param name="texturePath">マスクテクスチャのパス</param>
	void SetMaskTexture(const std::string& texturePath);

	/// <summary>抜く閾値を返す</summary>
	/// <returns>閾値（0〜1）</returns>
	float GetThreshold() const { return threshold_; }

private:
	// マスク取得元の種類
	enum class MaskMode : int
	{
		Texture = 0,  // テクスチャから取得
		GPUNoise = 1, // Shaderで擬似乱数を生成
	};

	struct DissolveParamsCB
	{
		float threshold;     // 4
		float edgeWidth;     // 4
		float useNoise;      // 4 (Shader側はfloatで判定)
		float noiseScale;    // 4 → 16バイト
		float edgeColor[4];  // 16バイト（rgb + intensity）
		float fillColor[4];  // 16バイト（rgb + intensity）
		float noiseTime;     // 4
		float _pad[3];       // 12 → 16バイト
	};

	float threshold_ = 0.0f;
	float edgeWidth_ = 0.03f;
	float edgeColor_[3] = { 1.0f, 0.4f, 0.3f };
	float edgeIntensity_ = 1.0f;
	float fillColor_[3] = { 0.0f, 0.0f, 0.0f };
	float fillIntensity_ = 1.0f;

	MaskMode maskMode_ = MaskMode::Texture;
	float noiseScale_ = 16.0f;
	float noiseTime_ = 0.0f;

	std::string maskTexturePath_ = "Resources/Textures/MaskTexture/noise0.dds";
	uint32_t maskSrvIndex_ = 0;

	DirectXCore* dxCore_ = nullptr;
};
