#pragma once
#include <wrl.h>
#include <d3d12.h>
#include <string>
#include <cstdint>

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
	/// 初期化（パイプラインとcbufferを作成する）
	/// </summary>
	/// <param name="dxCore">DirectXCoreへのポインタ</param>
	/// <param name="copyRootSignature">cbufferなし用ルートシグネチャ</param>
	/// <param name="effectRootSignature">cbufferあり用ルートシグネチャ</param>
	/// <param name="basePsoDesc">共通のパイプラインステート設定</param>
	virtual void Initialize(
		DirectXCore* dxCore,
		ID3D12RootSignature* copyRootSignature,
		ID3D12RootSignature* effectRootSignature,
		const D3D12_GRAPHICS_PIPELINE_STATE_DESC& basePsoDesc
	) = 0;

	/// <summary>
	/// 定数バッファを更新する（描画前に呼ばれる）
	/// </summary>
	virtual void UpdateConstantBuffer() = 0;

	/// <summary>
	/// ImGuiでパラメータを表示・編集する
	/// </summary>
	virtual void ShowImGui() = 0;

	/// <summary>
	/// パラメータをデフォルト値にリセットする
	/// </summary>
	virtual void ResetParams() = 0;

	/// <summary>
	/// エフェクト名を返す（"Gaussian", "Grayscale"等）
	/// </summary>
	virtual std::string GetName() const = 0;

	/// <summary>
	/// 定数バッファを使うかどうか
	/// </summary>
	virtual bool NeedsCBuffer() const = 0;

	// ===== 共通関数（オーバーライドしない） =====

	/// <summary>
	/// エフェクトを有効/無効にする
	/// </summary>
	void SetEnabled(bool enabled) { enabled_ = enabled; }

	/// <summary>
	/// エフェクトが有効かどうか
	/// </summary>
	bool IsEnabled() const { return enabled_; }

	/// <summary>
	/// パイプラインステートを取得
	/// </summary>
	ID3D12PipelineState* GetPipelineState() const { return pipelineState_.Get(); }

	/// <summary>
	/// 定数バッファのGPUアドレスを取得
	/// </summary>
	D3D12_GPU_VIRTUAL_ADDRESS GetConstantBufferGPUAddress() const
	{
		return constantBuffer_ ? constantBuffer_->GetGPUVirtualAddress() : 0;
	}

	/// <summary>
	/// 終了処理
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
	/// 定数バッファを作成してマップする
	/// </summary>
	/// <param name="dxCore">DirectXCoreへのポインタ</param>
	/// <param name="dataSize">定数バッファの実データサイズ</param>
	void CreateConstantBuffer(DirectXCore* dxCore, uint32_t dataSize);

	// 子クラスがアクセスする共通データ
	bool enabled_ = false;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState_;
	Microsoft::WRL::ComPtr<ID3D12Resource> constantBuffer_;
	void* constantBufferMappedPtr_ = nullptr;
};
