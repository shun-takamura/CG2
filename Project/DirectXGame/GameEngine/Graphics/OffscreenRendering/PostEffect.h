#pragma once
#include <wrl.h>
#include <d3d12.h>
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <algorithm>
#include <variant>
#include "RenderTexture.h"

// 前方宣言
class DirectXCore;
class SRVManager;

// ===== パラメータの型 =====
using ParamValue = std::variant<int, float, bool>;

/// <summary>
/// エフェクト1つ分の情報
/// </summary>
struct EffectEntry
{
	std::string name;                                                // エフェクト名（"Grayscale", "Sepia"等）
	bool enabled = false;                                            // ON/OFF
	Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState;       // パイプラインステート
	bool needsCBuffer = false;                                       // 定数バッファを使うか
	Microsoft::WRL::ComPtr<ID3D12Resource> constantBuffer;           // 定数バッファリソース
	void* constantBufferMappedPtr = nullptr;                         // マップ済みポインタ
	uint32_t constantBufferSize = 0;                                 // 定数バッファの実データサイズ
	std::unordered_map<std::string, ParamValue> params;              // パラメータ（名前→値）
};

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
	/// <param name="dxCore">DirectXCoreへのポインタ</param>
	/// <param name="srvManager">SRVManagerへのポインタ</param>
	/// <param name="width">画面幅</param>
	/// <param name="height">画面高さ</param>
	void Initialize(DirectXCore* dxCore, SRVManager* srvManager, uint32_t width, uint32_t height);

	/// <summary>
	/// シーン描画の開始（RenderTextureへの描画を開始する）
	/// </summary>
	/// <param name="commandList">コマンドリスト</param>
	/// <param name="dsvHandle">深度ステンシルビューのハンドル</param>
	void BeginSceneRender(ID3D12GraphicsCommandList* commandList, D3D12_CPU_DESCRIPTOR_HANDLE* dsvHandle = nullptr);

	/// <summary>
	/// シーン描画の終了（RenderTextureへの描画を終了する）
	/// </summary>
	/// <param name="commandList">コマンドリスト</param>
	void EndSceneRender(ID3D12GraphicsCommandList* commandList);

	/// <summary>
	/// マルチパス描画（ONのエフェクトを順番に適用してSwapchainに出力）
	/// </summary>
	/// <param name="commandList">コマンドリスト</param>
	void Draw(ID3D12GraphicsCommandList* commandList);

	/// <summary>
	/// 終了処理
	/// </summary>
	void Finalize();

	/// <summary>
	/// ImGuiでパラメータを表示・編集（ON/OFF、順番入れ替え、パラメータ調整）
	/// </summary>
	void ShowImGui();

	// ===== ゲームコード用API =====

	/// <summary>
	/// エフェクトのON/OFFを設定
	/// </summary>
	/// <param name="name">エフェクト名</param>
	/// <param name="enabled">true=ON, false=OFF</param>
	void SetEffectEnabled(const std::string& name, bool enabled);

	/// <summary>
	/// エフェクトのパラメータを設定（float）
	/// </summary>
	void SetEffectParam(const std::string& name, const std::string& param, float value);

	/// <summary>
	/// エフェクトのパラメータを設定（int）
	/// </summary>
	void SetEffectParam(const std::string& name, const std::string& param, int value);

	/// <summary>
	/// エフェクトの適用順序を設定
	/// </summary>
	/// <param name="order">エフェクト名のリスト（先頭から順に適用）</param>
	void SetEffectOrder(const std::vector<std::string>& order);

	/// <summary>
	/// 全エフェクトをOFFにしてパラメータをリセット
	/// </summary>
	void ResetEffects();

	/// <summary>
	/// ダメージ演出を適用（複数エフェクトをまとめて設定するヘルパー）
	/// </summary>
	/// <param name="damageRatio">0.0 = 満タン, 1.0 = 瀕死</param>
	void ApplyDamageEffect(float damageRatio);

	/// <summary>
	/// エフェクトがONかどうかを取得
	/// </summary>
	bool IsEffectEnabled(const std::string& name) const;

	/// <summary>
	/// エフェクトリストを取得（読み取り専用）
	/// </summary>
	const std::vector<EffectEntry>& GetEffectList() const { return effects_; }

private:
	// ===== 初期化系 =====
	void CreateRootSignatures();
	void CreatePipelineStates();
	void RegisterEffect(const std::string& name,
		const wchar_t* psPath,
		bool needsCBuffer,
		uint32_t cbufferDataSize,
		const std::unordered_map<std::string, ParamValue>& defaultParams);

	// ===== 定数バッファ系 =====
	Microsoft::WRL::ComPtr<ID3D12Resource> CreateConstantBuffer(uint32_t dataSize);
	void UpdateEffectConstantBuffer(EffectEntry& effect);

	// ===== 描画ヘルパー =====
	void DrawEffect(ID3D12GraphicsCommandList* commandList, EffectEntry& effect, RenderTexture* input);
	void DrawCopy(ID3D12GraphicsCommandList* commandList, RenderTexture* input);

	// ===== エフェクト検索 =====
	EffectEntry* FindEffect(const std::string& name);
	const EffectEntry* FindEffect(const std::string& name) const;

private:
	DirectXCore* dxCore_ = nullptr;
	SRVManager* srvManager_ = nullptr;

	// ピンポンRenderTexture（シーン描画用 + エフェクト作業用）
	std::unique_ptr<RenderTexture> renderTextureA_;
	std::unique_ptr<RenderTexture> renderTextureB_;

	// ルートシグネチャ（2種類）
	Microsoft::WRL::ComPtr<ID3D12RootSignature> copyRootSignature_;    // cbufferなし
	Microsoft::WRL::ComPtr<ID3D12RootSignature> effectRootSignature_;  // cbufferあり

	// コピー用パイプライン（エフェクトなし時 or 最終出力用）
	Microsoft::WRL::ComPtr<ID3D12PipelineState> copyPipelineState_;

	// エフェクトリスト（この順番で適用される）
	std::vector<EffectEntry> effects_;

	// 画面サイズ
	uint32_t width_ = 0;
	uint32_t height_ = 0;
};
