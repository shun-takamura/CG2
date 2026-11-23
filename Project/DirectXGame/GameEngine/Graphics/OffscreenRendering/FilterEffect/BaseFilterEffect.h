#pragma once
#include <wrl.h>
#include <d3d12.h>
#include <string>
#include <cstdint>
#include "Matrix4x4.h"

// 前方宣言
class DirectXCore;

/// <summary>
/// ポストエフェクトフィルタの基底クラス
/// 各エフェクト（Grayscale, Gaussian, Vignette等）はこれを継承して作る
/// </summary>
class BaseFilterEffect
{
public:
	virtual ~BaseFilterEffect() = default;

	// ===== 各エフェクトが必ずオーバーライドする関数 =====

	/// <summary>
	/// 初期化。派生は自分の PS で PSO を作り（基底の pipelineState_ に入れる）、
	/// 定数バッファを使うなら CreateConstantBuffer で作る。
	/// 深度・マスク・専用ルートシグネチャを使う派生は別の初期化関数を持ち、これは空の実装にする
	/// </summary>
	/// <param name="dxCore">DirectXCoreへのポインタ</param>
	/// <param name="copyRootSignature">cbufferなし用ルートシグネチャ（[0]=t0 のテーブル、s0=linear）</param>
	/// <param name="effectRootSignature">cbufferあり用ルートシグネチャ（[0]=t0 のテーブル、[1]=b0 の CBV、s0=linear）</param>
	/// <param name="basePsoDesc">共通のパイプラインステート設定（VS=PostProcess.VS、入力レイアウトなし、RTV は sRGB 1 枚）</param>
	virtual void Initialize(
		DirectXCore* dxCore,
		ID3D12RootSignature* copyRootSignature,
		ID3D12RootSignature* effectRootSignature,
		const D3D12_GRAPHICS_PIPELINE_STATE_DESC& basePsoDesc
	) = 0;

	/// <summary>
	/// 派生が持つパラメータを、マップ済みの定数バッファ（b0）へ書き込む。
	/// NeedsCBuffer() が true のとき、PostEffect がそのエフェクトを描く直前に呼ぶ
	/// </summary>
	virtual void UpdateConstantBuffer() = 0;

	/// <summary>派生の専用パラメータを ImGui で表示・編集する（PostEffect の一覧で有効なときだけ呼ばれる）</summary>
	virtual void ShowImGui() = 0;

	/// <summary>派生の専用パラメータを既定値に戻す（有効/無効は変えない）</summary>
	virtual void ResetParams() = 0;

	/// <summary>エフェクト名を返す。PostEffect::SetEffectOrder の並び替えと ImGui の表示名に使う</summary>
	/// <returns>エフェクト名（"Gaussian"、"Grayscale" など）</returns>
	virtual std::string GetName() const = 0;

	/// <summary>定数バッファ（b0）を使うかどうか。派生ごとに固定値を返す</summary>
	/// <returns>true なら描画前に UpdateConstantBuffer を呼び、b0 に CBV をバインドする</returns>
	virtual bool NeedsCBuffer() const = 0;

	/// <summary>
	/// シーン深度を参照するかどうか（Outline 系・RadialBlur・Distortion が true を返す）。
	/// true のエフェクトが 1 つでも有効なら、PostEffect が深度を PIXEL_SHADER_RESOURCE に遷移し、
	/// outline 用ルートシグネチャの t1 に深度 SRV をバインドして SetProjectionMatrix も呼ぶ
	/// </summary>
	/// <returns>深度を読むなら true</returns>
	virtual bool NeedsDepth() const { return false; }

	/// <summary>
	/// 深度以外の追加テクスチャ（t1）を参照するかどうか（Dissolve・Masked 系・LightShaft・Glitch 等が true）。
	/// NeedsDepth が true なら t1 は深度になるので、深度でないテクスチャを使う場合のみオーバーライドする
	/// </summary>
	/// <returns>追加テクスチャを読むなら true</returns>
	virtual bool NeedsMaskTexture() const { return false; }

	/// <summary>
	/// 追加テクスチャの SRV インデックスを返す。outline 用ルートシグネチャでは t1、
	/// MaskedOutline 用では t2、Distortion 用では t1 にバインドされる
	/// </summary>
	/// <returns>SRV インデックス。使わない派生は 0</returns>
	virtual uint32_t GetMaskTextureSRVIndex() const { return 0; }

	/// <summary>
	/// カメラの射影行列を受け取る（深度から視空間の距離を戻す派生だけが実装する）。
	/// PostEffect::SetProjectionMatrix から、NeedsDepth() が true のエフェクトにだけ毎フレーム Draw 前に呼ばれる
	/// </summary>
	/// <param name="projection">カメラの射影行列</param>
	virtual void SetProjectionMatrix([[maybe_unused]] const Matrix4x4& projection) {}

	// ===== 共通関数（オーバーライドしない） =====

	/// <summary>エフェクトを有効/無効にする（有効なものだけ PostEffect::Draw のチェーンに入る）</summary>
	/// <param name="enabled">true で有効</param>
	void SetEnabled(bool enabled) { enabled_ = enabled; }

	/// <summary>エフェクトが有効かどうかを返す</summary>
	/// <returns>有効なら true</returns>
	bool IsEnabled() const { return enabled_; }

	/// <summary>派生が Initialize で作った PSO を返す</summary>
	/// <returns>パイプラインステート（未作成なら nullptr）</returns>
	ID3D12PipelineState* GetPipelineState() const { return pipelineState_.Get(); }

	/// <summary>定数バッファ（b0）の GPU アドレスを返す</summary>
	/// <returns>GPU 仮想アドレス。定数バッファが無ければ 0</returns>
	D3D12_GPU_VIRTUAL_ADDRESS GetConstantBufferGPUAddress() const
	{
		return constantBuffer_ ? constantBuffer_->GetGPUVirtualAddress() : 0;
	}

	/// <summary>
	/// 定数バッファのマップを解除し、PSO と定数バッファを解放する。
	/// 派生で追加のリソースを持つ場合はオーバーライドし、最後に基底の Finalize を呼ぶ
	/// </summary>
	virtual void Finalize()
	{
		if (constantBuffer_ && constantBufferMappedPtr_) {
			constantBuffer_->Unmap(0, nullptr);
			constantBufferMappedPtr_ = nullptr;
		}
		pipelineState_.Reset();
		constantBuffer_.Reset();
	}

protected:
	// ===== 定数バッファ作成ヘルパー（子クラスのInitializeで使う） =====

	/// <summary>
	/// アップロードヒープに定数バッファを作り、マップしたままにする（constantBufferMappedPtr_ に書けば反映される）
	/// </summary>
	/// <param name="dxCore">DirectXCoreへのポインタ</param>
	/// <param name="dataSize">定数バッファの実データサイズ [byte]（256 byte 境界に切り上げて確保する）</param>
	void CreateConstantBuffer(DirectXCore* dxCore, uint32_t dataSize);

	// 子クラスがアクセスする共通データ
	bool enabled_ = false;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState_;
	Microsoft::WRL::ComPtr<ID3D12Resource> constantBuffer_;
	void* constantBufferMappedPtr_ = nullptr;
};
