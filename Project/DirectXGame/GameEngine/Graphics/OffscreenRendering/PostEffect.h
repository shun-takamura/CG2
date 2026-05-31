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
#include "VignetteEffect.h"
#include "SmoothingEffect.h"
#include "RadialBlurEffect.h"
#include "DissolveEffect.h"
#include "OutlineDepthEffect.h"
#include "OutlineNormalEffect.h"
#include "MaskedGrayscaleEffect.h"
#include "ColorInvertEffect.h"
#include "DisruptorRevealEffect.h"
#include "PrecisionBlurEffect.h"
#include "DistortionEffect.h"
#include "Matrix4x4.h"

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
	/// <param name="commandList">コマンドリスト</param>
	/// <param name="outputTarget">最終出力先のRenderTexture（nullptr ならSwapchainに出力）。
	/// Debugビルドで ImGui の ViewportWindow に表示するときに使う。</param>
	void Draw(ID3D12GraphicsCommandList* commandList, RenderTexture* outputTarget = nullptr);

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

	// シーン描画先のRenderTextureを取得（Skyboxなどで使用）
	RenderTexture* GetSceneRenderTarget() const { return renderTextureA_.get(); }

	// IDマスク用 RT（R8_UINT）。シーン描画後の ID Pass で書き込み、MaskedGrayscale 等の参照対象。
	RenderTexture* GetIdMaskRT() const { return idMaskRT_.get(); }

	// Distortion 用 RT（R8G8B8A8_UNORM）。RG=歪み方向, A=強度。Distortion Effect の参照対象。
	RenderTexture* GetDistortionRT() const { return distortionRT_.get(); }

	/// <summary>
	/// ID Pass の開始：idMaskRT を RT としてバインドし 0 でクリア。シーン描画後・PostEffect Draw 前に呼ぶ。
	/// </summary>
	void BeginIdPass(ID3D12GraphicsCommandList* commandList);
	/// <summary>
	/// ID Pass の終了：idMaskRT を PIXEL_SHADER_RESOURCE 状態に遷移。
	/// </summary>
	void EndIdPass(ID3D12GraphicsCommandList* commandList);

	/// <summary>
	/// Distortion Pass の開始：distortionRT を RT としてバインドし (0.5, 0.5, 0.5, 0) でクリア。
	/// IdPass の後・PostEffect Draw 前に呼ぶ。
	/// </summary>
	void BeginDistortionPass(ID3D12GraphicsCommandList* commandList);
	/// <summary>
	/// Distortion Pass の終了：distortionRT を PIXEL_SHADER_RESOURCE 状態に遷移。
	/// </summary>
	void EndDistortionPass(ID3D12GraphicsCommandList* commandList);

	/// <summary>
	/// プレビュー用：指定された color SRV と distortion SRV を入力に、現在バインドされている RTV に対して
	/// Distortion.PS を 1 パス実行する。EffectEditor 等で独自 RT に歪み合成したい場合に使う。
	/// 呼び出し側で RTV / Viewport / Scissor を設定済みであること。
	/// </summary>
	void RunDistortionForPreview(ID3D12GraphicsCommandList* commandList,
		uint32_t colorSrvIndex, uint32_t distortionSrvIndex);

	/// <summary>
	/// ダメージ演出を適用
	/// </summary>
	void ApplyDamageEffect(float damageRatio);

	/// <summary>
	/// カメラの射影行列を渡す（Outline系エフェクトで使用）
	/// 毎フレームDraw前に呼ぶ
	/// </summary>
	void SetProjectionMatrix(const Matrix4x4& projection);

	// ===== エフェクトへの直接アクセス =====

	GrayscaleEffect* grayscale = nullptr;
	GaussianEffect* gaussian = nullptr;
	SepiaEffect* sepia = nullptr;
	VignetteEffect* vignette = nullptr;
	SmoothingEffect* smoothing = nullptr;
	RadialBlurEffect* radialBlur = nullptr;
	DissolveEffect* dissolve = nullptr;
	OutlineDepthEffect* outlineDepth = nullptr;
	OutlineNormalEffect* outlineNormal = nullptr;
	MaskedGrayscaleEffect* maskedGrayscale = nullptr;
	ColorInvertEffect* colorInvert = nullptr;
	DisruptorRevealEffect* disruptorReveal = nullptr;
	PrecisionBlurEffect* precisionBlur = nullptr;
	DistortionEffect* distortion = nullptr;

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

	// IDマスク RT（R8_UINT、シーン描画後の ID Pass で書き込まれる）
	std::unique_ptr<RenderTexture> idMaskRT_;

	// Distortion 用 RT（R8G8B8A8_UNORM、Distortion Pass で歪み源プリミティブが書き込む）
	std::unique_ptr<RenderTexture> distortionRT_;

	// ルートシグネチャ（4種類）
	Microsoft::WRL::ComPtr<ID3D12RootSignature> copyRootSignature_;
	Microsoft::WRL::ComPtr<ID3D12RootSignature> effectRootSignature_;
	// outlineRootSignature: color SRV(t0) + depth SRV(t1) + cbuffer(b0) + linear(s0) + point(s1)
	Microsoft::WRL::ComPtr<ID3D12RootSignature> outlineRootSignature_;
	// distortionRootSignature: color SRV(t0) + distortion SRV(t1) + depth SRV(t2) + cbuffer(b0) + linear(s0) + point(s1)
	// distortion 合成パス専用。outlineRootSignature と分けることで他フィルタに影響しない。
	Microsoft::WRL::ComPtr<ID3D12RootSignature> distortionRootSignature_;

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
