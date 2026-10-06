#pragma once
#include "BaseFilterEffect.h"
#include "Vector3.h"
#include "Vector4.h"
#include <array>
#include <memory>

class RenderTexture;
class SRVManager;

/// <summary>
/// エフェクト用ブルーム。シーン全体の明るさから光る所を抜き出すのではなく、
/// Bloom パスで「光らせたいエフェクトだけ」を描いた発光 RT（HDR）を入力にする。
/// 発光 RT を 1/2 → 1/4 → 1/8 → 1/16 と縮小しながら各段をガウスぼかしし、
/// 全段を重み付きでシーンに加算する（小さい段ほど広く柔らかい光になる）。
/// LDR のシーン RT でも空や白い物が光らないのが LightShaft / 輝度抽出型との違い。
/// 有効/無効は Game.cpp / EffectEditor がエフェクトの状態から毎フレーム自動で切り替える。
/// </summary>
class BloomEffect : public BaseFilterEffect
{
public:
	static constexpr uint32_t kLevelCount = 4;
	// 発光 RT と縮小段のフォーマット（1.0 を超える強度を保つため float）。
	// PrimitivePipeline / GPUParticleManager の Bloom PSO の RTV フォーマットと一致させること。
	static constexpr DXGI_FORMAT kFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;

	BloomEffect();
	// RenderTexture を前方宣言で unique_ptr 保持しているため .cpp で定義する
	~BloomEffect() override;

	void InitializeBloom(
		DirectXCore* dxCore,
		SRVManager* srvManager,
		const D3D12_GRAPHICS_PIPELINE_STATE_DESC& basePsoDesc,
		uint32_t width,
		uint32_t height
	);

	// Base 経由でも呼べるようダミー実装（未使用）
	void Initialize(
		DirectXCore* /*dxCore*/,
		ID3D12RootSignature* /*copyRootSignature*/,
		ID3D12RootSignature* /*effectRootSignature*/,
		const D3D12_GRAPHICS_PIPELINE_STATE_DESC& /*basePsoDesc*/
	) override {}

	// パラメータはルート定数で渡すので定数バッファは持たない
	void UpdateConstantBuffer() override {}
	void ShowImGui() override;
	void ResetParams() override;
	void Finalize() override;

	std::string GetName() const override { return "Bloom"; }
	bool NeedsCBuffer() const override { return false; }

	/// <summary>
	/// 発光 RT から縮小＋ぼかしの各段を作る。ポストエフェクトのチェーンを描く前に呼ぶ（RTV を切り替える）。
	/// sourceWidth/Height は発光 RT のサイズ（最初の縮小のサンプル間隔に使う）。
	/// </summary>
	void Prepare(ID3D12GraphicsCommandList* commandList, uint32_t sourceSrvIndex,
		uint32_t sourceWidth, uint32_t sourceHeight);

	/// <summary>
	/// 現在バインドされている RTV に「シーン＋ブルーム」を描く。Prepare 済みであること。
	/// </summary>
	void Composite(ID3D12GraphicsCommandList* commandList, uint32_t sceneSrvIndex);

	// ===== パラメータ =====
	void SetIntensity(float v) { intensity_ = v; }
	void SetTint(const Vector3& c) { tint_ = c; }
	float GetIntensity() const { return intensity_; }

private:
	// HLSL の BloomParams（Bloom.hlsli）と並びを一致させる。ルート定数で渡す。
	struct Params
	{
		float texelSize[2];
		float direction[2];
		Vector4 levelWeights;
		Vector3 tint;
		float intensity;
	};
	static constexpr uint32_t kParamCount = sizeof(Params) / sizeof(uint32_t);

	void CreateRootSignature(DirectXCore* dxCore);
	void CreatePipelines(DirectXCore* dxCore, const D3D12_GRAPHICS_PIPELINE_STATE_DESC& basePsoDesc);

	// t0..t4 をまとめてバインドし、全画面三角形を1枚描く
	void DrawFullscreen(ID3D12GraphicsCommandList* commandList, ID3D12PipelineState* pso,
		const uint32_t (&srvs)[kLevelCount + 1], const Params& params);

	SRVManager* srvManager_ = nullptr;

	Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> downsamplePso_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> blurPso_;
	// 合成 PSO は基底の pipelineState_ を使う（チェーン出力と同じ sRGB フォーマット）

	// 各段：ping=縮小結果→最終ぼかし結果 / pong=横ぼかしの中間
	struct Level
	{
		std::unique_ptr<RenderTexture> ping;
		std::unique_ptr<RenderTexture> pong;
	};
	std::array<Level, kLevelCount> levels_;

	float   intensity_ = 1.0f;
	Vector3 tint_ = { 1.0f, 1.0f, 1.0f };
	Vector4 levelWeights_ = { 1.0f, 0.8f, 0.6f, 0.4f };
};
