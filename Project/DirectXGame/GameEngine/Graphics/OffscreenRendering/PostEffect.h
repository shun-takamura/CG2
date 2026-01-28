#pragma once
#include <wrl.h>
#include <d3d12.h>
#include <cstdint>

// 前方宣言
class DirectXCore;
class SRVManager;
class RenderTexture;

/// <summary>
/// ポストエフェクトクラス
/// RenderTextureの内容をSwapchainにコピーする機能を提供
/// 将来的にはここにブラー、ブルームなどのエフェクトを追加できる
/// </summary>
class PostEffect
{
public:
	/// <summary>
	/// 初期化
	/// </summary>
	/// <param name="dxCore">DirectXCoreへのポインタ</param>
	/// <param name="srvManager">SRVManagerへのポインタ</param>
	void Initialize(DirectXCore* dxCore, SRVManager* srvManager);

	/// <summary>
	/// 描画（RenderTextureの内容を現在のレンダーターゲットにコピー）
	/// </summary>
	/// <param name="commandList">コマンドリスト</param>
	/// <param name="renderTexture">コピー元のRenderTexture</param>
	void Draw(ID3D12GraphicsCommandList* commandList, RenderTexture* renderTexture);

	/// <summary>
	/// 終了処理
	/// </summary>
	void Finalize();

private:
	/// <summary>
	/// ルートシグネチャを作成
	/// </summary>
	void CreateRootSignature();

	/// <summary>
	/// パイプラインステートを作成
	/// </summary>
	void CreatePipelineState();

private:
	// DirectXCoreへのポインタ
	DirectXCore* dxCore_ = nullptr;

	// SRVManagerへのポインタ
	SRVManager* srvManager_ = nullptr;

	// ルートシグネチャ
	// シェーダーが使うリソース（テクスチャ、サンプラー等）のレイアウトを定義
	Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;

	// パイプラインステート
	// シェーダー、ブレンド、深度テストなどの描画設定をまとめたもの
	Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState_;
};
