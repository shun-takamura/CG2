#pragma once
#include <wrl.h>
#include <d3d12.h>
#include <cstdint>
#include <cassert>

// 前方宣言
class DirectXCore;
class SRVManager;

/// <summary>
/// オフスクリーンレンダリング用のレンダーテクスチャクラス
/// シーンを一度このテクスチャに描画し、後からシェーダーで読み取ることができる
/// ポストエフェクトの土台として使用する
/// </summary>
class RenderTexture
{
public:
	/// <summary>
	/// RENDER_TARGET 状態のテクスチャ（DEFAULT ヒープ、クリア値付き）と専用の RTV ヒープを作り、
	/// SRVManager から SRV を 1 つ確保する
	/// </summary>
	/// <param name="dxCore">DirectXCoreへのポインタ</param>
	/// <param name="srvManager">SRVManagerへのポインタ</param>
	/// <param name="width">テクスチャの幅 [px]</param>
	/// <param name="height">テクスチャの高さ [px]</param>
	/// <param name="format">テクスチャフォーマット（デフォルトはSRGB）</param>
	/// <param name="clearColor">クリア時の色（RGBA、0.0〜1.0）。nullptr なら既定色 (0.1, 0.25, 0.5, 1)</param>
	void Initialize(
		DirectXCore* dxCore,
		SRVManager* srvManager,
		uint32_t width,
		uint32_t height,
		DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
		const float clearColor[4] = nullptr
	);

	/// <summary>
	/// このRenderTextureへの描画を開始する
	/// リソースバリアをRENDER_TARGETに遷移し、クリア色でクリアして RTV・ビューポート・シザーをセットする
	/// </summary>
	/// <param name="commandList">コマンドリスト</param>
	/// <param name="dsvHandle">深度ステンシルビューのハンドル（nullptrで深度なし）</param>
	void BeginRender(ID3D12GraphicsCommandList* commandList, D3D12_CPU_DESCRIPTOR_HANDLE* dsvHandle = nullptr);

	/// <summary>
	/// このRenderTextureへの描画を終了する
	/// リソースバリアをPIXEL_SHADER_RESOURCEに遷移し、シェーダーから読めるようにする
	/// </summary>
	/// <param name="commandList">コマンドリスト</param>
	void EndRender(ID3D12GraphicsCommandList* commandList);

	/// <summary>
	/// 大きさを変えて作り直す（描画解像度の変更用）。SRV の番号はそのまま使い回すので、
	/// 番号を覚えている側は何もしなくてよい。GPU が古いリソースを使い終わってから呼ぶこと
	/// </summary>
	/// <param name="width">新しい幅 [px]（0 なら何もしない）</param>
	/// <param name="height">新しい高さ [px]（0 なら何もしない）</param>
	void Resize(uint32_t width, uint32_t height);

	/// <summary>
	/// SRV の番号を SRVManager に返し、リソースと RTV ヒープを解放する
	/// </summary>
	void Finalize();

	// ===== ゲッター =====

	/// <summary>
	/// SRVインデックスを取得（シェーダーでテクスチャとして使う際に必要）
	/// </summary>
	/// <returns>SRV インデックス。未割り当てなら UINT32_MAX</returns>
	uint32_t GetSRVIndex() const { return srvIndex_; }

	/// <summary>テクスチャリソースを返す</summary>
	/// <returns>リソース（未作成・解放後は nullptr）</returns>
	ID3D12Resource* GetResource() const { return resource_.Get(); }

	/// <summary>RTV の CPU ハンドルを返す</summary>
	/// <returns>RTV ハンドル</returns>
	D3D12_CPU_DESCRIPTOR_HANDLE GetRTVHandle() const { return rtvHandle_; }

	/// <summary>テクスチャの幅を返す</summary>
	/// <returns>幅 [px]</returns>
	uint32_t GetWidth() const { return width_; }

	/// <summary>テクスチャの高さを返す</summary>
	/// <returns>高さ [px]</returns>
	uint32_t GetHeight() const { return height_; }

	/// <summary>クリアカラーを返す</summary>
	/// <returns>要素 4 の配列（RGBA）</returns>
	const float* GetClearColor() const { return clearColor_; }

private:
	/// <summary>
	/// レンダーテクスチャ用のリソースを作成する（DEFAULT ヒープ・ALLOW_RENDER_TARGET・初期状態 RENDER_TARGET・クリア値付き）
	/// </summary>
	void CreateResource();

	/// <summary>
	/// 1 枚分の RTV ヒープを作り、そこに RTV（レンダーターゲットビュー）を作成する
	/// </summary>
	void CreateRTV();

	/// <summary>
	/// SRVManager から番号を確保して SRV（シェーダーリソースビュー）を作成する
	/// </summary>
	void CreateSRV();

private:
	// DirectXCoreへのポインタ
	DirectXCore* dxCore_ = nullptr;

	// SRVManagerへのポインタ
	SRVManager* srvManager_ = nullptr;

	// テクスチャリソース本体
	// RTV（書き込み用）とSRV（読み取り用）の両方で使用される
	Microsoft::WRL::ComPtr<ID3D12Resource> resource_;

	// RTV専用のディスクリプタヒープ
	// RenderTextureごとに1つ持つ（シンプルな設計）
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> rtvHeap_;

	// RTVのCPUハンドル（描画先として設定する際に使用）
	D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle_{};

	// SRVのインデックス（SRVManagerで管理）。UINT32_MAXは未割り当て
	uint32_t srvIndex_ = UINT32_MAX;

	// テクスチャの幅
	uint32_t width_ = 0;

	// テクスチャの高さ
	uint32_t height_ = 0;

	// テクスチャフォーマット
	DXGI_FORMAT format_ = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;

	// クリア時の色（RGBA）
	// ClearValueとClearRenderTargetViewで同じ値を使うことで最適化される
	float clearColor_[4] = { 0.1f, 0.25f, 0.5f, 1.0f };

	// 現在のリソースステート
	// DX12ではリソースの状態遷移を自分で管理する必要がある
	D3D12_RESOURCE_STATES currentState_ = D3D12_RESOURCE_STATE_RENDER_TARGET;
};
