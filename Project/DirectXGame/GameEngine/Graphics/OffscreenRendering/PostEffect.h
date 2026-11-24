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
#include "ScreenDropletEffect.h"
#include "SmoothingEffect.h"
#include "RadialBlurEffect.h"
#include "DissolveEffect.h"
#include "OutlineDepthEffect.h"
#include "OutlineNormalEffect.h"
#include "MaskedGrayscaleEffect.h"
#include "MaskedOutlineEffect.h"
#include "LightShaftEffect.h"
#include "GlitchEffect.h"
#include "ColorInvertEffect.h"
#include "PrecisionBlurEffect.h"
#include "DistortionEffect.h"
#include "BloomEffect.h"
#include "MonochromeEffect.h"
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
	/// シーン（ピンポン A/B）・ID マスク・歪み・Bloom・キャプチャ・静止画の各 RT、
	/// ルートシグネチャ 5 種、共通 PSO 設定と全エフェクトを作る
	/// </summary>
	/// <param name="dxCore">DirectXCoreへのポインタ</param>
	/// <param name="srvManager">RT の SRV 確保とテーブルのバインドに使う SRVManager</param>
	/// <param name="width">描画解像度の幅 [px]</param>
	/// <param name="height">描画解像度の高さ [px]</param>
	/// <param name="sceneClearColor">シーン RT のクリア色（RGBA 4要素）。nullptr なら (0,0,0,0)。
	/// アプリの背景色をここに渡すと、何も描かれていない領域がその色で埋まる。</param>
	void Initialize(DirectXCore* dxCore, SRVManager* srvManager, uint32_t width, uint32_t height,
		const float sceneClearColor[4] = nullptr);

	/// <summary>
	/// 中の RT（シーン・ID・歪み・Bloom・キャプチャ・静止画）と Bloom の縮小段を描画解像度に合わせて作り直す。
	/// SRV の番号は変わらない。GPU が古い RT を使い終わってから（フレームの頭で）呼ぶこと
	/// </summary>
	/// <param name="width">新しい幅 [px]（0 や同じ大きさなら何もしない）</param>
	/// <param name="height">新しい高さ [px]（0 や同じ大きさなら何もしない）</param>
	void Resize(uint32_t width, uint32_t height);

	/// <summary>
	/// 時間で動くエフェクトを進める（今は白黒の演出 Monochrome の再生だけ）。毎フレーム 1 回、実時間で呼ぶ
	/// </summary>
	/// <param name="deltaTime">経過時間 [s]</param>
	void Update(float deltaTime);

	/// <summary>描画解像度の幅を返す</summary>
	/// <returns>幅 [px]</returns>
	uint32_t GetWidth() const { return width_; }
	/// <summary>描画解像度の高さを返す</summary>
	/// <returns>高さ [px]</returns>
	uint32_t GetHeight() const { return height_; }

	/// <summary>
	/// シーン描画の開始。シーン RT（renderTextureA_）をクリアして描画先にする
	/// </summary>
	/// <param name="commandList">コマンドリスト</param>
	/// <param name="dsvHandle">深度ステンシルビューのハンドル（nullptr で深度なし）</param>
	void BeginSceneRender(ID3D12GraphicsCommandList* commandList, D3D12_CPU_DESCRIPTOR_HANDLE* dsvHandle = nullptr);

	/// <summary>
	/// シーン描画の終了。シーン RT を PIXEL_SHADER_RESOURCE に遷移する
	/// </summary>
	/// <param name="commandList">コマンドリスト</param>
	void EndSceneRender(ID3D12GraphicsCommandList* commandList);

	/// <summary>
	/// マルチパス描画。有効なエフェクトを並び順に、シーン RT を入力としてピンポン RT で順に掛け、
	/// 最後のパスを最終出力へ描く（有効なものが無ければコピーだけ）
	/// </summary>
	/// <param name="commandList">コマンドリスト</param>
	/// <param name="outputTarget">最終出力先のRenderTexture（nullptr ならSwapchainに出力）。
	/// Debugビルドで ImGui の ViewportWindow に表示するときに使う。</param>
	void Draw(ID3D12GraphicsCommandList* commandList, RenderTexture* outputTarget = nullptr);

	/// <summary>
	/// 全エフェクトの Finalize、ルートシグネチャ・コピー PSO の解放、全 RT の Finalize を行う
	/// </summary>
	void Finalize();

	/// <summary>
	/// エフェクト一覧（ON/OFF・ドラッグで並び替え・有効なものはパラメータ）と Bloom のパラメータを ImGui で表示する。
	/// Distortion / Bloom は自動制御なので ON/OFF の対象外
	/// </summary>
	void ShowImGui();

	/// <summary>
	/// エフェクトの適用順序を設定する。order に無いエフェクトは今の順のまま末尾に回す
	/// </summary>
	/// <param name="order">先に掛けたいエフェクト名（GetName の値）の並び</param>
	void SetEffectOrder(const std::vector<std::string>& order);

	/// <summary>
	/// 全エフェクトをOFFにしてパラメータをリセット
	/// </summary>
	void ResetEffects();

	/// <summary>シーン描画先の RenderTexture を返す（Skybox などで使用）</summary>
	/// <returns>シーン RT</returns>
	RenderTexture* GetSceneRenderTarget() const { return renderTextureA_.get(); }

	/// <summary>IDマスク用 RT（R8_UINT）を返す。シーン描画後の ID Pass で書き込み、MaskedGrayscale 等の参照対象</summary>
	/// <returns>ID マスク RT</returns>
	RenderTexture* GetIdMaskRT() const { return idMaskRT_.get(); }

	/// <summary>Distortion 用 RT（R8G8B8A8_UNORM。RG=歪み方向, A=強度）を返す。Distortion Effect の参照対象</summary>
	/// <returns>歪み RT</returns>
	RenderTexture* GetDistortionRT() const { return distortionRT_.get(); }

	/// <summary>Bloom 用の発光 RT（R16G16B16A16_FLOAT）を返す。Bloom Pass で光らせたいエフェクトだけが書き込む</summary>
	/// <returns>発光 RT</returns>
	RenderTexture* GetBloomRT() const { return bloomRT_.get(); }

	/// <summary>シーンキャプチャ RT（画面を割る演出の破片が反転して貼る元絵）を返す</summary>
	/// <returns>キャプチャ RT</returns>
	RenderTexture* GetCaptureRT() const { return captureRT_.get(); }
	/// <summary>シーンキャプチャ RT の SRV インデックスを返す</summary>
	/// <returns>SRV インデックス。キャプチャ RT が無ければ 0</returns>
	uint32_t GetCaptureSRVIndex() const;

	/// <summary>
	/// 現フレームのシーン（renderTextureA_）を captureRT_ にコピーする。
	/// シーン描画途中（renderTextureA_ が RENDER_TARGET 状態）に呼ぶ前提。終了後はシーン RTV/深度/ビューポートを復帰する。
	/// 画面を割る演出（ゲームの必殺技 Shatterline）で、破片が貼り付ける「反転世界の元絵」を1枚取るのに使う。
	/// </summary>
	/// <param name="commandList">コマンドリスト</param>
	void CaptureScene(ID3D12GraphicsCommandList* commandList);

	// ----- 静止画の 1 枚（ボス撃破の瞬間 → リザルトの背景。23_StageClear.md §3）-----
	// ポストエフェクト（ブルーム・色調）を掛け終えた絵を stillCaptureRT_ に残す。シーンの切り替えをまたいで残る。
	// このエンジンは UI もシーン RT に描くので、UI を除きたいなら撮るフレームはシーン側が UI を描かないこと。
	/// <summary>次の Draw で最後のパスを stillCaptureRT_ に書き、それを最終出力へコピーする</summary>
	void RequestStillCapture() { stillCaptureRequested_ = true; }
	/// <summary>撮れた後に 1 度だけ true を返し、フラグを下ろす（Draw で記録した後）</summary>
	/// <returns>撮れた直後の 1 回だけ true</returns>
	bool ConsumeStillCaptureDone();
	/// <summary>ポストエフェクト後の静止画 RT を返す</summary>
	/// <returns>静止画 RT</returns>
	RenderTexture* GetStillCaptureRT() const { return stillCaptureRT_.get(); }

	/// <summary>
	/// ID Pass の開始：idMaskRT を RT としてバインドし 0 でクリア。シーン描画後・PostEffect Draw 前に呼ぶ。
	/// </summary>
	/// <param name="commandList">コマンドリスト</param>
	void BeginIdPass(ID3D12GraphicsCommandList* commandList);
	/// <summary>
	/// ID Pass の終了：idMaskRT を PIXEL_SHADER_RESOURCE 状態に遷移。
	/// </summary>
	/// <param name="commandList">コマンドリスト</param>
	void EndIdPass(ID3D12GraphicsCommandList* commandList);

	/// <summary>
	/// Distortion Pass の開始：distortionRT を RT としてバインドし (0.5, 0.5, 0.5, 0) でクリア。
	/// IdPass の後・PostEffect Draw 前に呼ぶ。
	/// </summary>
	/// <param name="commandList">コマンドリスト</param>
	void BeginDistortionPass(ID3D12GraphicsCommandList* commandList);
	/// <summary>
	/// Distortion Pass の終了：distortionRT を PIXEL_SHADER_RESOURCE 状態に遷移。
	/// </summary>
	/// <param name="commandList">コマンドリスト</param>
	void EndDistortionPass(ID3D12GraphicsCommandList* commandList);

	/// <summary>
	/// Bloom Pass の開始：bloomRT を 0 でクリアし、RTV＋ビューポートをバインドする。Distortion Pass の後に呼ぶ。
	/// dsvHandle を渡すとシーン深度でテストされ、手前の物に隠れた発光は描かれない。
	/// </summary>
	/// <param name="commandList">コマンドリスト</param>
	/// <param name="dsvHandle">シーン深度の DSV ハンドル（nullptr で深度テストなし）</param>
	void BeginBloomPass(ID3D12GraphicsCommandList* commandList, D3D12_CPU_DESCRIPTOR_HANDLE* dsvHandle = nullptr);
	/// <summary>
	/// Bloom Pass の終了：bloomRT を PIXEL_SHADER_RESOURCE 状態に遷移。
	/// </summary>
	/// <param name="commandList">コマンドリスト</param>
	void EndBloomPass(ID3D12GraphicsCommandList* commandList);

	/// <summary>
	/// プレビュー用：指定された color SRV と distortion SRV を入力に、現在バインドされている RTV に対して
	/// Distortion.PS を 1 パス実行する（distortion 専用 RS：[0]=t0 色、[1]=t1 歪み、[2]=t2 シーン深度、[3]=b0）。
	/// EffectEditor 等で独自 RT に歪み合成したい場合に使う。
	/// 呼び出し側で RTV / Viewport / Scissor を設定済みであること。
	/// </summary>
	/// <param name="commandList">コマンドリスト</param>
	/// <param name="colorSrvIndex">t0 に入れる色の SRV インデックス</param>
	/// <param name="distortionSrvIndex">t1 に入れる歪みマップの SRV インデックス</param>
	void RunDistortionForPreview(ID3D12GraphicsCommandList* commandList,
		uint32_t colorSrvIndex, uint32_t distortionSrvIndex);

	/// <summary>
	/// ダメージ演出を適用する（ダメージ率が 0.9 以上ならグレースケールを ON、未満なら OFF）
	/// </summary>
	/// <param name="damageRatio">ダメージ率（0〜1 に切り詰める）</param>
	void ApplyDamageEffect(float damageRatio);

	/// <summary>
	/// カメラの射影行列を、NeedsDepth() が true のエフェクト（Outline 系など）へ渡す
	/// 毎フレームDraw前に呼ぶ
	/// </summary>
	/// <param name="projection">カメラの射影行列</param>
	void SetProjectionMatrix(const Matrix4x4& projection);

	// ===== エフェクトへの直接アクセス =====

	GrayscaleEffect* grayscale = nullptr;
	GaussianEffect* gaussian = nullptr;
	SepiaEffect* sepia = nullptr;
	VignetteEffect* vignette = nullptr;
	ScreenDropletEffect* screenDroplet = nullptr; // 画面に付いた水滴（濡れ具合で自動で有効/無効）
	SmoothingEffect* smoothing = nullptr;
	RadialBlurEffect* radialBlur = nullptr;
	DissolveEffect* dissolve = nullptr;
	OutlineDepthEffect* outlineDepth = nullptr;
	OutlineNormalEffect* outlineNormal = nullptr;
	MaskedGrayscaleEffect* maskedGrayscale = nullptr;
	LightShaftEffect* lightShaft = nullptr;
	GlitchEffect* glitch = nullptr;  // 空間がずれたようなノイズ（HUB の機体設定の入れ替えなど）
	MaskedOutlineEffect* maskedOutline = nullptr;
	ColorInvertEffect* colorInvert = nullptr;
	PrecisionBlurEffect* precisionBlur = nullptr;
	DistortionEffect* distortion = nullptr;
	BloomEffect* bloom = nullptr;
	MonochromeEffect* monochrome = nullptr; // 墨絵のような白黒の演出（ボスの必殺技など。Play で自動 ON）

private:
	/// <summary>
	/// ルートシグネチャ 5 種を作る（全て PS 可視・サンプラは clamp）。
	/// copy：[0]=t0、s0=linear／effect：[0]=t0、[1]=b0 の CBV、s0=linear／
	/// outline：[0]=t0、[1]=t1、[2]=b0、s0=linear、s1=point／
	/// distortion：[0]=t0、[1]=t1 歪み、[2]=t2 深度、[3]=b0、s0/s1／
	/// maskedOutline：[0]=t0、[1]=t1 深度、[2]=t2 ID マスク、[3]=b0、s0/s1
	/// </summary>
	void CreateRootSignatures();
	/// <summary>
	/// エフェクト共通の PSO 設定（入力レイアウトなし・ブレンドなし・カリングなし・深度なし・RTV は sRGB 1 枚・VS=PostProcess.VS）と、
	/// コピー用 PSO（CopyImage.VS/PS、copy 用ルートシグネチャ）を作る
	/// </summary>
	void CreateBasePsoDesc();
	/// <summary>
	/// 全エフェクトを作って初期化し、並び順に登録する（使うルートシグネチャはエフェクトごとに選ぶ）。Bloom はチェーンの先頭に入れる
	/// </summary>
	void InitializeEffects();

	// 描画ヘルパー
	/// <summary>
	/// 1 エフェクト分を全画面三角形で描く。エフェクトの種類（Bloom / Distortion / MaskedOutline / 深度・マスクあり / それ以外）に応じて
	/// ルートシグネチャを選び、t0 に入力、補助テクスチャと b0 をそれぞれの番号へバインドする
	/// </summary>
	/// <param name="commandList">コマンドリスト</param>
	/// <param name="effect">描くエフェクト</param>
	/// <param name="input">t0 に入れる入力 RT</param>
	void DrawEffect(ID3D12GraphicsCommandList* commandList, BaseFilterEffect* effect, RenderTexture* input);
	/// <summary>入力 RT を今バインドされている RTV へそのままコピーする（copy 用ルートシグネチャの t0）</summary>
	/// <param name="commandList">コマンドリスト</param>
	/// <param name="input">コピー元の RT</param>
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

	// Bloom 用の発光 RT（R16G16B16A16_FLOAT、Bloom Pass で bloom 指定のエフェクトが書き込む）
	std::unique_ptr<RenderTexture> bloomRT_;

	// シーンキャプチャ RT（ディスラプター崩壊の破片用。renderTextureA_ のコピー先）
	std::unique_ptr<RenderTexture> captureRT_;

	// 静止画の 1 枚（ポストエフェクトの後の絵。RequestStillCapture）
	std::unique_ptr<RenderTexture> stillCaptureRT_;
	bool stillCaptureRequested_ = false;
	bool stillCaptureDone_ = false;

	// ルートシグネチャ（4種類）
	Microsoft::WRL::ComPtr<ID3D12RootSignature> copyRootSignature_;
	Microsoft::WRL::ComPtr<ID3D12RootSignature> effectRootSignature_;
	// outlineRootSignature: color SRV(t0) + depth SRV(t1) + cbuffer(b0) + linear(s0) + point(s1)
	Microsoft::WRL::ComPtr<ID3D12RootSignature> outlineRootSignature_;
	// distortionRootSignature: color SRV(t0) + distortion SRV(t1) + depth SRV(t2) + cbuffer(b0) + linear(s0) + point(s1)
	// distortion 合成パス専用。outlineRootSignature と分けることで他フィルタに影響しない。
	Microsoft::WRL::ComPtr<ID3D12RootSignature> distortionRootSignature_;
	// maskedOutlineRootSignature: color SRV(t0) + depth SRV(t1) + idMask SRV(t2) + cbuffer(b0) + linear(s0) + point(s1)
	// MaskedOutline 専用（深度とマスクの両方を要るため outlineRootSignature では足りない）。
	Microsoft::WRL::ComPtr<ID3D12RootSignature> maskedOutlineRootSignature_;

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
