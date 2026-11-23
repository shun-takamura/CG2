#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include <wrl.h>
#include <d3d12.h>

#include "Vector2.h"
#include "Vector4.h"

class DirectXCore;
class SRVManager;
class FontAtlas;

// 画面座標（左上原点・ピクセル）に UTF-8 文字列を描画するシングルトン。
// インスタンシング描画（StructuredBuffer + DrawInstanced(4, N)）で
// 1 フレーム分のグリフを 1 DrawCall に集約する。
//
// 積む先（層）は 3 つ。それぞれ 1 フレームに 1 回 Flush する（インスタンスバッファは層ごとに別の区画）。
//   通常    : DrawText / DrawRect → Flush（シーンの最後）
//   発光    : DrawTextGlow / DrawRectGlow → FlushGlow（Game の発光パスが呼ぶ。ブルームの発光 RT に加算）
//   画面の外: BeginOffscreen〜FlushOffscreen の間の DrawText / DrawRect（3D の板に貼る RenderTexture へ描く）
/// <summary>
/// 画面・発光・画面の外へ文字と四角を積み、まとめて描く。
/// シングルトンにする理由：フォントのアトラスと文字の描画パイプラインを 1 組だけ持ち、どこからでも文字を積めるようにするため。
/// </summary>
class TextRenderer {
public:
	/// <summary>シングルトンインスタンスを返す</summary>
	/// <returns>TextRenderer のインスタンス</returns>
	static TextRenderer* GetInstance();

	/// <summary>
	/// フォントアトラスを作り、ルートシグネチャ・PSO 2 種（通常/発光）・インスタンスバッファを作って、
	/// 既定の文字セットを bake しておく。アトラスの作成に失敗したら初期化済みにならない（以後の描画は何もしない）
	/// </summary>
	/// <param name="dxCore">DirectXCoreへのポインタ</param>
	/// <param name="srvManager">アトラスとインスタンスバッファの SRV を確保する SRVManager</param>
	/// <param name="ttfPath">フォント（TTF）のパス</param>
	/// <param name="pixelHeight">bake 解像度（グリフの高さ [px]）</param>
	/// <param name="atlasSize">アトラスの一辺 [px]</param>
	void Initialize(DirectXCore* dxCore, SRVManager* srvManager,
		const std::string& ttfPath, int pixelHeight = 32, int atlasSize = 1024);

	/// <summary>インスタンスバッファのアンマップ、アトラス・PSO・ルートシグネチャの解放、積んだ層の破棄を行う</summary>
	void Finalize();

	/// <summary>
	/// 描画要求を 1 フレームバッファ（今の層）に積む。
	/// outlineThickness > 0 のとき、各グリフを 8 方向にオフセット複製してアウトラインを付ける。
	/// 太いほどメモリ消費が増える（グリフ × 9 インスタンス）ので使いどころに注意。
	/// </summary>
	/// <param name="utf8">描く文字列（UTF-8）</param>
	/// <param name="pos">左上のピクセル座標（UI 座標。画面の外の層なら RT のピクセル）</param>
	/// <param name="scale">フォントサイズ倍率</param>
	/// <param name="color">文字色（RGBA）</param>
	/// <param name="outlineThickness">アウトラインの太さ（0 以下でなし）</param>
	/// <param name="outlineColor">アウトラインの色（RGBA）</param>
	void DrawText(std::string_view utf8, const Vector2& pos, float scale = 1.0f,
		const Vector4& color = { 1.0f, 1.0f, 1.0f, 1.0f },
		float outlineThickness = 0.0f,
		const Vector4& outlineColor = { 0.0f, 0.0f, 0.0f, 1.0f });

	/// <summary>塗りつぶしの四角（線・枠・ゲージ用）を今の層に積む。文字と同じ 1 DrawCall に入る</summary>
	/// <param name="pos">左上のピクセル座標</param>
	/// <param name="size">大きさ [px]</param>
	/// <param name="color">色（RGBA）</param>
	void DrawRect(const Vector2& pos, const Vector2& size, const Vector4& color);

	/// <summary>文字を発光の層に積む（ブルームの発光 RT に描く）</summary>
	/// <param name="utf8">描く文字列（UTF-8）</param>
	/// <param name="pos">左上のピクセル座標（UI 座標）</param>
	/// <param name="scale">フォントサイズ倍率</param>
	/// <param name="color">色（1 を超えてよい＝強さ）</param>
	void DrawTextGlow(std::string_view utf8, const Vector2& pos, float scale, const Vector4& color);
	/// <summary>塗りつぶしの四角を発光の層に積む</summary>
	/// <param name="pos">左上のピクセル座標（UI 座標）</param>
	/// <param name="size">大きさ [px]</param>
	/// <param name="color">色（1 を超えてよい＝強さ）</param>
	void DrawRectGlow(const Vector2& pos, const Vector2& size, const Vector4& color);

	/// <summary>
	/// 以後に積む物の位置と大きさを center を中心に scale 倍する（ResetTransform まで。全部の層に効く）。
	/// ブラウン管が切れるように UI を縦に潰す、などに使う
	/// </summary>
	/// <param name="center">拡大縮小の中心（ピクセル座標）</param>
	/// <param name="scale">X・Y それぞれの倍率</param>
	void SetTransform(const Vector2& center, const Vector2& scale);
	/// <summary>SetTransform の拡大縮小をやめる</summary>
	void ResetTransform();

	/// <summary>文字列の描画ピクセル幅を取得（レイアウト用。未 bake の文字は bake する）</summary>
	/// <param name="utf8">文字列（UTF-8）</param>
	/// <param name="scale">フォントサイズ倍率</param>
	/// <returns>送り幅の合計 [px]。未初期化なら 0</returns>
	float MeasureWidth(std::string_view utf8, float scale = 1.0f);

	/// <summary>
	/// フレームの描画の頭で呼ぶ（インスタンスバッファの書き込み位置を先頭へ戻す）。
	/// 1 フレームに同じ層を何度 Flush しても、前の Flush の分を上書きしないよう続きへ書くため
	/// </summary>
	void BeginFrame();

	/// <summary>
	/// シーンが描画を終えた最後に呼び出す。通常の層を UI の仮想解像度で射影して今の RTV へ描く。
	/// 内部で bake 待ちをアトラスに反映し、最終的に 1 つの DrawCall を発行する。
	/// 1 フレームに何度呼んでもよい（続きの位置へ書く。区画からあふれた分は描かない）
	/// </summary>
	void Flush();

	/// <summary>発光の層に積んだ物があるかを返す</summary>
	/// <returns>あれば true</returns>
	bool HasPendingGlow() const { return !layers_[kLayerGlow].empty(); }
	/// <summary>発光の層を、今バインドされている発光 RT（BloomEffect::kFormat）へ加算で描く。Game の発光パスが呼ぶ</summary>
	void FlushGlow();

	/// <summary>
	/// 画面の外（RenderTexture。sRGB の RGBA8）へ描き始める。以後の DrawText / DrawRect は
	/// この層に積まれ、FlushOffscreen で今バインドされている RT へ描く（座標は RT のピクセル）
	/// </summary>
	/// <param name="width">描く先の RT の幅 [px]</param>
	/// <param name="height">描く先の RT の高さ [px]</param>
	void BeginOffscreen(float width, float height);
	/// <summary>画面の外の層を今バインドされている RT へ描き、積む先を通常の層に戻す</summary>
	void FlushOffscreen();

	/// <summary>フォントアトラスを返す</summary>
	/// <returns>アトラス（初期化に失敗していたら nullptr）</returns>
	FontAtlas* GetAtlas() { return atlas_.get(); }

	/// <summary>初期化済みかを返す</summary>
	/// <returns>初期化に成功していれば true</returns>
	bool IsInitialized() const { return initialized_; }

private:
	TextRenderer() = default;
	~TextRenderer() = default;
	TextRenderer(const TextRenderer&) = delete;
	TextRenderer& operator=(const TextRenderer&) = delete;

	// HLSL の GlyphInstance とレイアウト一致。outlineWidth < 0 は塗りつぶしの四角（アトラスを読まない）
	struct GlyphInstance {
		float screenPosX, screenPosY;
		float sizeX, sizeY;
		float u0, v0, u1, v1;
		float r, g, b, a;
		float outR, outG, outB, outA;
		float outlineWidth;
		float _pad0, _pad1, _pad2;
	};

	enum Layer : uint32_t {
		kLayerScreen = 0,
		kLayerGlow,
		kLayerOffscreen,
		kLayerCount
	};

	/// <summary>
	/// ルートシグネチャを作る。[0]=VS の t0（インスタンスの StructuredBuffer）、[1]=VS b0 のルート定数 4 個
	/// （描く先の幅・高さ [px]・層の区画の先頭インスタンス番号）、[2]=PS の t0（アトラス）、s0=linear clamp
	/// </summary>
	void CreateRootSignature();
	/// <summary>Text.VS/PS で PSO を 2 つ作る（通常＝sRGB の RGBA8 へ αブレンド、発光＝BloomEffect::kFormat へ加算）</summary>
	void CreatePipelineState();
	/// <summary>層の数 × kMaxInstances 分のインスタンスバッファを作ってマップし、StructuredBuffer の SRV を作る</summary>
	void CreateInstanceBuffer();

	/// <summary>文字列を 1 文字ずつグリフのインスタンスにして積む（未 bake の文字は bake する。上限を超えた分は捨てる）</summary>
	/// <param name="out">積む先の層</param>
	/// <param name="utf8">文字列（UTF-8）</param>
	/// <param name="pos">左上のピクセル座標</param>
	/// <param name="scale">フォントサイズ倍率</param>
	/// <param name="color">文字色（RGBA）</param>
	/// <param name="outlineThickness">アウトラインの太さ（0 以下でなし）</param>
	/// <param name="outlineColor">アウトラインの色（RGBA）</param>
	void PushText(std::vector<GlyphInstance>& out, std::string_view utf8, const Vector2& pos, float scale,
		const Vector4& color, float outlineThickness, const Vector4& outlineColor);
	/// <summary>塗りつぶしの四角のインスタンス（outlineWidth=-1、アトラスを読まない）を積む</summary>
	/// <param name="out">積む先の層</param>
	/// <param name="pos">左上のピクセル座標</param>
	/// <param name="size">大きさ [px]</param>
	/// <param name="color">色（RGBA）</param>
	void PushRect(std::vector<GlyphInstance>& out, const Vector2& pos, const Vector2& size, const Vector4& color);
	/// <summary>通常の DrawText / DrawRect の積む先を返す（BeginOffscreen の間は画面の外）</summary>
	/// <returns>今の層のバッファ</returns>
	std::vector<GlyphInstance>& CurrentLayer() { return layers_[offscreenActive_ ? kLayerOffscreen : kLayerScreen]; }
	/// <summary>
	/// アトラスへ bake 待ちを反映し、層をインスタンスバッファの自分の区画（このフレームで書いた続き）へ写して
	/// 三角形ストリップ 4 頂点 × インスタンス数で描く。描いた層は空にする
	/// </summary>
	/// <param name="layer">描く層</param>
	/// <param name="pso">使う PSO</param>
	/// <param name="targetWidth">射影に使う描く先の幅 [px]</param>
	/// <param name="targetHeight">射影に使う描く先の高さ [px]</param>
	void DrawLayer(Layer layer, ID3D12PipelineState* pso, float targetWidth, float targetHeight);

	DirectXCore* dxCore_ = nullptr;
	SRVManager* srvManager_ = nullptr;
	std::unique_ptr<FontAtlas> atlas_;

	bool initialized_ = false;

	Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState_;       // 通常・画面の外（sRGB の RGBA8、αブレンド）
	Microsoft::WRL::ComPtr<ID3D12PipelineState> glowPipelineState_;   // 発光 RT（float、加算）

	// 1 層あたりの最大インスタンス数。バッファは層の数だけ区画を持つ
	static constexpr uint32_t kMaxInstances = 4096;

	Microsoft::WRL::ComPtr<ID3D12Resource> instanceResource_;
	GlyphInstance* instanceData_ = nullptr;
	uint32_t instanceSrvIndex_ = 0;

	std::array<std::vector<GlyphInstance>, kLayerCount> layers_; // フレーム内バッファ
	// 層ごとの、このフレームで区画に書き終えた数（GPU はフレームの最後に読むので、同じフレームの Flush は続きへ書く）
	std::array<uint32_t, kLayerCount> layerWritten_{};

	bool offscreenActive_ = false;
	float offscreenWidth_ = 0.0f;
	float offscreenHeight_ = 0.0f;

	bool hasTransform_ = false;
	Vector2 transformCenter_{};
	Vector2 transformScale_{ 1.0f, 1.0f };
};
