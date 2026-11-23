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

	/// <summary>Glitch.PS の PSO を outline 用ルートシグネチャで作り、定数バッファを作る</summary>
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

	/// <summary>強さ・マスクの掛け方・調整値と、経過時間から求めた seed・描画解像度の縦横比を定数バッファへ書き込む</summary>
	void UpdateConstantBuffer() override;
	/// <summary>強さ・マスクの掛け方・調整値を ImGui で編集する（Auto Time が ON なら時間も進める）</summary>
	void ShowImGui() override;
	/// <summary>強さ 0・画面全体・調整値を既定に戻す（時間は戻さない）</summary>
	void ResetParams() override;

	/// <summary>エフェクト名を返す</summary>
	/// <returns>"Glitch"</returns>
	std::string GetName() const override { return "Glitch"; }
	/// <summary>定数バッファを使う</summary>
	/// <returns>常に true</returns>
	bool NeedsCBuffer() const override { return true; }
	/// <summary>ID マスク（t1）を使う</summary>
	/// <returns>常に true</returns>
	bool NeedsMaskTexture() const override { return true; }
	/// <summary>t1 にバインドする ID マスク RT の SRV インデックスを返す</summary>
	/// <returns>SRV インデックス。idMaskRT が無ければ 0</returns>
	uint32_t GetMaskTextureSRVIndex() const override;

	/// <summary>強さを設定する（0 で何もしない）</summary>
	/// <param name="intensity">強さ（0〜1。書き込み時に切り詰める）</param>
	void SetIntensity(float intensity) { intensity_ = intensity; }
	/// <summary>強さを返す</summary>
	/// <returns>強さ</returns>
	float GetIntensity() const { return intensity_; }
	/// <summary>どこに掛けるか（画面全体 / マスクの中 / マスクの外）を設定する</summary>
	/// <param name="mode">掛ける範囲</param>
	void SetMaskMode(MaskMode mode) { maskMode_ = mode; }
	/// <summary>どこに掛けるかを返す</summary>
	/// <returns>掛ける範囲</returns>
	MaskMode GetMaskMode() const { return maskMode_; }
	/// <summary>経過時間を設定する（ずれ方の切り替えに使う）。呼ぶ側が毎フレーム進める</summary>
	/// <param name="seconds">経過時間 [秒]</param>
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
	/// <summary>グリッチの調整値（書き換える時はコピーして SetParams で戻す）</summary>
	/// <returns>今の値</returns>
	const Params& GetParams() const { return params_; }
	/// <summary>グリッチの調整値をまとめて差し替える</summary>
	/// <param name="value">新しい値</param>
	void SetParams(const Params& value) { params_ = value; }

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
