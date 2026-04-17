#pragma once
#include <wrl.h>
#include <d3d12.h>
#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include <algorithm>

#include "RenderTexture.h"
#include "BaseFilterEffect.h"
#include "GrayscaleEffect.h"
#include "GaussianEffect.h"
#include "SepiaEffect.h"

// 前方宣言
class DirectXCore;
class SRVManager;

/// <summary>
/// ポストエフェクトクラス（マルチパス対応）
/// 複数のエフェクトをピンポンRenderTextureで順番に適用する
/// </summary>
class PostEffect
{
public:
	/// <summary>
	/// 初期化
	/// </summary>
	void Initialize(DirectXCore* dxCore, SRVManager* srvManager, uint32_t width, uint32_t height);

	/// <summary>
	/// シーン描画の開始
	/// </summary>
	void BeginSceneRender(ID3D12GraphicsCommandList* commandList, D3D12_CPU_DESCRIPTOR_HANDLE* dsvHandle = nullptr);

	/// <summary>
	/// シーン描画の終了
	/// </summary>
	void EndSceneRender(ID3D12GraphicsCommandList* commandList);

	/// <summary>
	/// マルチパス描画
	/// </summary>
	void Draw(ID3D12GraphicsCommandList* commandList);

	/// <summary>
	/// 終了処理
	/// </summary>
	void Finalize();

	/// <summary>
	/// ImGui表示
	/// </summary>
	void ShowImGui();

	/// <summary>
	/// エフェクトの適用順序を設定
	/// </summary>
	void SetEffectOrder(const std::vector<std::string>& order);

	/// <summary>
	/// 全エフェクトをOFFにしてパラメータをリセット
	/// </summary>
	void ResetEffects();

	/// <summary>
	/// ダメージ演出を適用
	/// </summary>
	void ApplyDamageEffect(float damageRatio);

	// ===== エフェクトへの直接アクセス =====

	GrayscaleEffect* grayscale = nullptr;
	GaussianEffect* gaussian = nullptr;
	SepiaEffect* sepia = nullptr;

private:
	void CreateRootSignatures();
	void CreateBasePsoDesc();
	void InitializeEffects();

	// 描画ヘルパー
	void DrawEffect(ID3D12GraphicsCommandList* commandList, BaseFilterEffect* effect, RenderTexture* input);
	void DrawCopy(ID3D12GraphicsCommandList* commandList, RenderTexture* input);

private:
	DirectXCore* dxCore_ = nullptr;
	SRVManager* srvManager_ = nullptr;

	// ピンポンRenderTexture
	std::unique_ptr<RenderTexture> renderTextureA_;
	std::unique_ptr<RenderTexture> renderTextureB_;

	// ルートシグネチャ（2種類）
	Microsoft::WRL::ComPtr<ID3D12RootSignature> copyRootSignature_;
	Microsoft::WRL::ComPtr<ID3D12RootSignature> effectRootSignature_;

	// コピー用パイプライン
	Microsoft::WRL::ComPtr<ID3D12PipelineState> copyPipelineState_;

	// 共通PSO設定（エフェクト初期化時に渡す）
	D3D12_GRAPHICS_PIPELINE_STATE_DESC basePsoDesc_{};

	// エフェクトリスト（この順番で適用される）
	// 各エフェクトのunique_ptrを保持し、生ポインタをpublicメンバに設定
	std::vector<std::unique_ptr<BaseFilterEffect>> effectOwners_;
	std::vector<BaseFilterEffect*> effectOrder_;

	// 画面サイズ
	uint32_t width_ = 0;
	uint32_t height_ = 0;
};
