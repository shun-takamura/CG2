#pragma once
#include <wrl.h>
#include <d3d12.h>
#include <cstdint>

// 前方宣言
class DirectXCore;
class SRVManager;
class RenderTexture;

/// <summary>
/// ポストエフェクトのパラメータ構造体
/// シェーダーの定数バッファと同じレイアウトにする必要がある
/// HLSLのfloat3は16バイトアライメントされるため注意
/// </summary>
struct PostProcessParams
{
	float grayscaleIntensity = 0.0f;  // offset: 0
	float sepiaIntensity = 0.0f;      // offset: 4
	float sepiaColor[3] = { 1.0f, 0.691f, 0.402f };  // offset: 8, 12, 16
	float _padding1 = 0.0f;           // offset: 20 (float3の後のパディング)
	float vignetteIntensity = 0.0f;  // offset: 24
	float vignettePower = 0.8f;       // offset: 28
	float vignetteScale = 16.0f;      // offset: 32
	float _padding2 = 0.0f;           // offset: 36 (アライメント用)
};

/// <summary>
/// ポストエフェクトクラス
/// RenderTextureの内容にエフェクトを適用してSwapchainに描画
/// </summary>
class PostEffect
{
public:
	/// <summary>
	/// 初期化
	/// </summary>
	void Initialize(DirectXCore* dxCore, SRVManager* srvManager);

	/// <summary>
	/// 描画（RenderTextureの内容を現在のレンダーターゲットに描画）
	/// </summary>
	void Draw(ID3D12GraphicsCommandList* commandList, RenderTexture* renderTexture);

	/// <summary>
	/// 終了処理
	/// </summary>
	void Finalize();

	/// <summary>
	/// ImGuiでパラメータを表示・編集
	/// </summary>
	void ShowImGui();

	/// <summary>
	/// パラメータを取得
	/// </summary>
	PostProcessParams& GetParams() { return params_; }

private:
	void CreateCopyRootSignature();
	void CreateEffectRootSignature();
	void CreatePipelineStates();
	void CreateConstantBuffer();
	void UpdateConstantBuffer();

private:
	DirectXCore* dxCore_ = nullptr;
	SRVManager* srvManager_ = nullptr;

	// ルートシグネチャ（2種類）
	Microsoft::WRL::ComPtr<ID3D12RootSignature> copyRootSignature_;    // Copy用（定数バッファなし）
	Microsoft::WRL::ComPtr<ID3D12RootSignature> effectRootSignature_;  // エフェクト用（定数バッファあり）

	// パイプラインステート（5種類）
	Microsoft::WRL::ComPtr<ID3D12PipelineState> copyPipelineState_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> combinedPipelineState_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> grayscalePipelineState_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> sepiaPipelineState_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> vignettePipelineState_;

	// 定数バッファ
	Microsoft::WRL::ComPtr<ID3D12Resource> constantBuffer_;
	PostProcessParams* constantBufferMappedPtr_ = nullptr;

	// パラメータ
	PostProcessParams params_;

	// 現在のエフェクトタイプ（ImGuiで選択）
	int currentEffectType_ = 0;  // 0:Copy, 1:Combined, 2:Grayscale, 3:Sepia, 4:Vignette
};