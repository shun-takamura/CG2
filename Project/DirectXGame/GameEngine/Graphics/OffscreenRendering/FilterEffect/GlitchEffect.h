#pragma once
#include "BaseFilterEffect.h"

class RenderTexture;

/// <summary>
/// 空間がずれたようなノイズ（グリッチ）。画面を斜めの帯に切って帯ごとに帯の向きへずらし、
/// 帯の縁で R と B を逆へずらして紫と緑の色ずれを出す。強いときは四角いブロック単位でもずらす。
/// ずれ方は jitterInterval 秒ごとに変わる（その間は止まって見える＝パッパッと切り替わる）。
/// どこに掛けるかは ID マスク（idMaskRT）で選ぶ：画面全体 / マスクの中だけ / マスクの外だけ。
/// 強さ（0..1）の上げ下げと、強さが最大の瞬間に中身を入れ替えるのは呼ぶ側（HUB の機体設定など）。
/// outline 用ルートシグネチャ（color t0 + idMask t1 + cbuffer b0）で作る（MaskedGrayscale と同じ）。
/// </summary>
class GlitchEffect : public BaseFilterEffect
{
public:
	enum class MaskMode : int {
		Screen = 0,   // 画面全体
		Inside = 1,   // ID マスクが 0 でない所だけ（例：機体だけ）
		Outside = 2,  // ID マスクが 0 の所だけ（例：機体を残して周りの神殿だけ）
	};

	void InitializeMasked(
		DirectXCore* dxCore,
		ID3D12RootSignature* outlineRootSignature,
		const D3D12_GRAPHICS_PIPELINE_STATE_DESC& basePsoDesc,
		RenderTexture* idMaskRT
	);

	// Base 経由でも呼べるようダミー実装（未使用。InitializeMasked を使う）
	void Initialize(
		DirectXCore* /*dxCore*/,
		ID3D12RootSignature* /*copyRootSignature*/,
		ID3D12RootSignature* /*effectRootSignature*/,
		const D3D12_GRAPHICS_PIPELINE_STATE_DESC& /*basePsoDesc*/
	) override {}

	void UpdateConstantBuffer() override;
	void ShowImGui() override;
	void ResetParams() override;

	std::string GetName() const override { return "Glitch"; }
	bool NeedsCBuffer() const override { return true; }
	bool NeedsMaskTexture() const override { return true; }
	uint32_t GetMaskTextureSRVIndex() const override;

	/// <summary>強さ 0..1（0 で何もしない）</summary>
	void SetIntensity(float intensity) { intensity_ = intensity; }
	float GetIntensity() const { return intensity_; }
	void SetMaskMode(MaskMode mode) { maskMode_ = mode; }
	MaskMode GetMaskMode() const { return maskMode_; }
	/// <summary>経過秒（ずれ方の切り替えに使う）。呼ぶ側が毎フレーム進める</summary>
	void SetTime(float seconds) { time_ = seconds; }

	// 見た目の調整値（呼ぶ側の JSON から入れる）
	struct Params {
		float bandSlope = -0.55f;      // 帯の傾き（画面の縦横比を直した後の dy/dx）。負で右下がり
		float bandCount = 18.0f;       // 画面の対角あたりの帯の数
		float bandChance = 0.55f;      // 強さ 1 のとき、ずれる帯の割合
		float maxShift = 0.06f;        // 帯のずれの最大（画面の幅に対する割合）
		float rgbSplit = 0.012f;       // 色ずれの量（画面の幅に対する割合）
		float blockSize = 48.0f;       // ブロックの一辺 [px]（描画解像度）
		float blockChance = 0.25f;     // 強さ 1 のとき、ずれるブロックの割合
		float tintStrength = 0.35f;    // ずれた帯を紫・緑に染める強さ
		float jitterInterval = 0.05f;  // ずれ方が変わる間隔 [s]
	};
	Params& GetParams() { return params_; }
	const Params& GetParams() const { return params_; }

private:
	struct ParamsCB
	{
		float intensity;
		float seed;          // jitterInterval ごとに変わる整数
		int   maskMode;
		float aspect;        // 幅 / 高さ
		float bandSlope;
		float bandCount;
		float bandChance;
		float maxShift;
		float rgbSplit;
		float blockSize;
		float blockChance;
		float tintStrength;
	};

	float intensity_ = 0.0f;
	MaskMode maskMode_ = MaskMode::Screen;
	float time_ = 0.0f;
	Params params_;
	DirectXCore* dxCore_ = nullptr;
	RenderTexture* idMaskRT_ = nullptr;  // PostEffect 所有
};
