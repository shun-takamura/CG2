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
	/// 初期化
	/// </summary>
	/// <param name="dxCore">DirectXCoreへのポインタ</param>
	/// <param name="srvManager">SRVManagerへのポインタ</param>
	/// <param name="width">テクスチャの幅</param>
	/// <param name="height">テクスチャの高さ</param>
	/// <param name="format">テクスチャフォーマット（デフォルトはSRGB）</param>
	/// <param name="clearColor">クリア時の色（RGBA、0.0〜1.0）</param>
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
	/// リソースバリアをRENDER_TARGETに遷移し、RTVをセットする
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
	/// 終了処理
	/// </summary>
	void Finalize();

	// ===== ゲッター =====

	/// <summary>
	/// SRVインデックスを取得（シェーダーでテクスチャとして使う際に必要）
	/// </summary>
	uint32_t GetSRVIndex() const { return srvIndex_; }

	/// <summary>
	/// テクスチャリソースを取得
	/// </summary>
	ID3D12Resource* GetResource() const { return resource_.Get(); }

	/// <summary>
	/// RTVハンドルを取得
	/// </summary>
	D3D12_CPU_DESCRIPTOR_HANDLE GetRTVHandle() const { return rtvHandle_; }

	/// <summary>
	/// テクスチャの幅を取得
	/// </summary>
	uint32_t GetWidth() const { return width_; }

	/// <summary>
	/// テクスチャの高さを取得
	/// </summary>
	uint32_t GetHeight() const { return height_; }

	/// <summary>
	/// クリアカラーを取得
	/// </summary>
	const float* GetClearColor() const { return clearColor_; }

private:
	/// <summary>
	/// レンダーテクスチャ用のリソースを作成する
	/// </summary>
	void CreateResource();

	/// <summary>
	/// RTV（レンダーターゲットビュー）を作成する
	/// </summary>
	void CreateRTV();

	/// <summary>
	/// SRV（シェーダーリソースビュー）を作成する
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

	// SRVのインデックス（SRVManagerで管理）
	// シェーダーからテクスチャとして読む際に使用
	uint32_t srvIndex_ = 0;

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
