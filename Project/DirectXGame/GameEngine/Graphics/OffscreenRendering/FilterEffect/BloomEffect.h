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
	/// <summary>RenderTexture を前方宣言で unique_ptr 保持しているため .cpp で定義するデストラクタ</summary>
	~BloomEffect() override;

	/// <summary>
	/// 専用ルートシグネチャ（[0]〜[4]=t0〜t4 の各テーブル、[5]=b0 のルート定数、s0=linear）と
	/// 縮小・ぼかし・合成の 3 つの PSO を作り、縮小段（1/2〜1/16）の RT を確保する
	/// </summary>
	/// <param name="dxCore">DirectXCoreへのポインタ</param>
	/// <param name="srvManager">縮小段の RT の SRV 確保とテーブルのバインドに使う SRVManager</param>
	/// <param name="basePsoDesc">共通のパイプラインステート設定（合成 PSO の RTV フォーマットはこれに合わせる）</param>
	/// <param name="width">発光 RT の幅 [px]</param>
	/// <param name="height">発光 RT の高さ [px]</param>
	void InitializeBloom(
		DirectXCore* dxCore,
		SRVManager* srvManager,
		const D3D12_GRAPHICS_PIPELINE_STATE_DESC& basePsoDesc,
		uint32_t width,
		uint32_t height
	);

	/// <summary>発光 RT の大きさが変わったときに縮小段を作り直す（描画解像度の変更用。SRV の番号は変わらない）</summary>
	/// <param name="width">新しい発光 RT の幅 [px]</param>
	/// <param name="height">新しい発光 RT の高さ [px]</param>
	void Resize(uint32_t width, uint32_t height);

	/// <summary>Base 経由でも呼べるようにしたダミー実装（未使用。初期化は InitializeBloom）</summary>
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

	/// <summary>何もしない（パラメータはルート定数で渡すので定数バッファは持たない）</summary>
	void UpdateConstantBuffer() override {}
	/// <summary>強さ・色味・各段の重みを ImGui で編集する</summary>
	void ShowImGui() override;
	/// <summary>強さ・色味・各段の重みを既定値に戻す（fade は戻さない）</summary>
	void ResetParams() override;
	/// <summary>縮小段の RT・縮小/ぼかし PSO・専用ルートシグネチャを解放し、基底の Finalize を呼ぶ</summary>
	void Finalize() override;

	/// <summary>エフェクト名を返す</summary>
	/// <returns>"Bloom"</returns>
	std::string GetName() const override { return "Bloom"; }
	/// <summary>定数バッファを使わない（ルート定数で渡す）</summary>
	/// <returns>常に false</returns>
	bool NeedsCBuffer() const override { return false; }

	/// <summary>
	/// 発光 RT から縮小＋ぼかしの各段を作る。ポストエフェクトのチェーンを描く前に呼ぶ（RTV を切り替える）。
	/// </summary>
	/// <param name="commandList">コマンドリスト</param>
	/// <param name="sourceSrvIndex">発光 RT の SRV インデックス</param>
	/// <param name="sourceWidth">発光 RT の幅 [px]（最初の縮小のサンプル間隔に使う）</param>
	/// <param name="sourceHeight">発光 RT の高さ [px]（最初の縮小のサンプル間隔に使う）</param>
	void Prepare(ID3D12GraphicsCommandList* commandList, uint32_t sourceSrvIndex,
		uint32_t sourceWidth, uint32_t sourceHeight);

	/// <summary>
	/// 現在バインドされている RTV に「シーン＋ブルーム」を描く。Prepare 済みであること。
	/// t0=シーン、t1〜t4=各縮小段、b0=重み・色味・強さ（強さには fade を掛ける）
	/// </summary>
	/// <param name="commandList">コマンドリスト</param>
	/// <param name="sceneSrvIndex">チェーン入力（シーン）の SRV インデックス</param>
	void Composite(ID3D12GraphicsCommandList* commandList, uint32_t sceneSrvIndex);

	// ===== パラメータ =====
	/// <summary>光の強さを設定する</summary>
	/// <param name="v">強さ（合成時に 0 未満は 0 として扱う）</param>
	void SetIntensity(float v) { intensity_ = v; }
	/// <summary>光に掛ける色味を設定する</summary>
	/// <param name="c">色味（RGB）</param>
	void SetTint(const Vector3& c) { tint_ = c; }
	/// <summary>光の強さを返す</summary>
	/// <returns>強さ</returns>
	float GetIntensity() const { return intensity_; }
	/// <summary>Intensity に掛ける率を設定する。トランジションの覆いの上に光が出ないよう Game.cpp が毎フレーム設定する</summary>
	/// <param name="v">率（0〜1。合成時に範囲外は切り詰める）</param>
	void SetFade(float v) { fade_ = v; }

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
	// ルートパラメータの番号。[0]〜[kLevelCount] が t0〜 のテーブル、その次が b0 のルート定数
	static constexpr uint32_t kRootParamConstants = kLevelCount + 1;
	// ルートパラメータの数（テーブル kLevelCount + 1 個 ＋ ルート定数 1 個）
	static constexpr uint32_t kRootParamCount = kLevelCount + 2;

	/// <summary>専用ルートシグネチャを作る（[0]〜[4]=t0〜t4 を 1 枚ずつのテーブル、[5]=b0 のルート定数 Params、s0=linear clamp）</summary>
	/// <param name="dxCore">DirectXCoreへのポインタ</param>
	void CreateRootSignature(DirectXCore* dxCore);
	/// <summary>縮小（BloomDownsample）・ぼかし（BloomBlur）を kFormat で、合成（BloomComposite）をチェーン出力の形式で作る</summary>
	/// <param name="dxCore">DirectXCoreへのポインタ</param>
	/// <param name="basePsoDesc">共通のパイプラインステート設定</param>
	void CreatePipelines(DirectXCore* dxCore, const D3D12_GRAPHICS_PIPELINE_STATE_DESC& basePsoDesc);

	/// <summary>t0〜t4 をまとめてバインドし、パラメータをルート定数で渡して全画面三角形を 1 枚描く</summary>
	/// <param name="commandList">コマンドリスト</param>
	/// <param name="pso">使う PSO（縮小・ぼかし・合成のどれか）</param>
	/// <param name="srvs">t0〜t4 にバインドする SRV インデックス</param>
	/// <param name="params">b0 に渡すパラメータ</param>
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
	float   fade_ = 1.0f;
	Vector3 tint_ = { 1.0f, 1.0f, 1.0f };
	Vector4 levelWeights_ = { 1.0f, 0.8f, 0.6f, 0.4f };
};
