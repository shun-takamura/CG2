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
	/// 専用ルートシグネチャ（color t0 + depth t1 + idMask t2 + cbuffer b0）で MaskedOutlineNormal.PS のパイプラインと定数バッファを作成。
	/// </summary>
	/// <param name="dxCore">DirectXCoreへのポインタ</param>
	/// <param name="maskedOutlineRootSignature">MaskedOutline 専用ルートシグネチャ（[0]=t0、[1]=t1、[2]=t2、[3]=b0、s0=linear、s1=point）</param>
	/// <param name="basePsoDesc">共通のパイプラインステート設定</param>
	/// <param name="idMaskRT">ID マスク RT（PostEffect 所有。t2 にバインドされる）</param>
	void InitializeMaskedOutline(
		DirectXCore* dxCore,
		ID3D12RootSignature* maskedOutlineRootSignature,
		const D3D12_GRAPHICS_PIPELINE_STATE_DESC& basePsoDesc,
		RenderTexture* idMaskRT
	);

	/// <summary>Base 経由でも呼べるようにしたダミー実装（未使用。初期化は InitializeMaskedOutline）</summary>
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

	/// <summary>射影逆行列・色・重み・閾値・点滅の設定を定数バッファへ書き込む（最低強度は 0〜1 に切り詰める）</summary>
	void UpdateConstantBuffer() override;
	/// <summary>色・重み・閾値・点滅の設定を ImGui で編集する</summary>
	void ShowImGui() override;
	/// <summary>色・重み・閾値・点滅の設定を既定値に戻す（時間は戻さない）</summary>
	void ResetParams() override;

	/// <summary>エフェクト名を返す</summary>
	/// <returns>"MaskedOutline"</returns>
	std::string GetName() const override { return "MaskedOutline"; }
	/// <summary>定数バッファを使う</summary>
	/// <returns>常に true</returns>
	bool NeedsCBuffer() const override { return true; }
	/// <summary>深度遷移＋射影行列伝播に乗せる（深度は専用ルートシグネチャの t1）</summary>
	/// <returns>常に true</returns>
	bool NeedsDepth() const override { return true; }
	/// <summary>ID マスクを使う（PostEffect は専用の分岐で t2 にバインドする）</summary>
	/// <returns>常に true</returns>
	bool NeedsMaskTexture() const override { return true; }
	/// <summary>t2 にバインドする ID マスク RT の SRV インデックスを返す</summary>
	/// <returns>SRV インデックス。idMaskRT が無ければ 0</returns>
	uint32_t GetMaskTextureSRVIndex() const override;

	/// <summary>射影行列の逆行列を保持する（深度から視空間の位置を戻すのに使う）</summary>
	/// <param name="projection">カメラの射影行列</param>
	void SetProjectionMatrix(const Matrix4x4& projection) override;

	// ===== 専用パラメータ設定 =====
	/// <summary>ID マスク 1（炎状態）のアウトライン色を設定する</summary>
	/// <param name="c">色（RGBA）</param>
	void SetFireColor(const Vector4& c) { colorFire_ = c; }
	/// <summary>ID マスク 2（氷状態）のアウトライン色を設定する</summary>
	/// <param name="c">色（RGBA）</param>
	void SetIceColor(const Vector4& c) { colorIce_ = c; }
	/// <summary>点滅に使う経過時間を設定する（毎フレーム外部から渡す）</summary>
	/// <param name="t">経過時間 [秒]</param>
	void SetTime(float t) { time_ = t; }
	/// <summary>点滅の速さを設定する</summary>
	/// <param name="v">点滅の周波数 [Hz]</param>
	void SetBlinkHz(float v) { blinkHz_ = v; }
	/// <summary>点滅で一番暗くなるときの強さを設定する</summary>
	/// <param name="v">最低強度（0〜1）</param>
	void SetMinIntensity(float v) { minIntensity_ = v; }
	/// <summary>深度差のエッジの重みを設定する</summary>
	/// <param name="v">重み</param>
	void SetDepthWeight(float v) { depthWeight_ = v; }
	/// <summary>法線差のエッジの重みを設定する</summary>
	/// <param name="v">重み</param>
	void SetNormalWeight(float v) { normalWeight_ = v; }
	/// <summary>深度差のエッジとみなす閾値を設定する</summary>
	/// <param name="v">閾値</param>
	void SetDepthThreshold(float v) { depthThreshold_ = v; }
	/// <summary>法線差のエッジとみなす閾値を設定する</summary>
	/// <param name="v">閾値</param>
	void SetNormalThreshold(float v) { normalThreshold_ = v; }
	/// <summary>エッジ全体の強さを設定する</summary>
	/// <param name="v">強さ</param>
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
