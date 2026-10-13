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
class TextRenderer {
public:
	static TextRenderer* GetInstance();

	void Initialize(DirectXCore* dxCore, SRVManager* srvManager,
		const std::string& ttfPath, int pixelHeight = 32, int atlasSize = 1024);

	void Finalize();

	// 描画要求を 1 フレームバッファに積む。
	// pos は左上のピクセル座標、scale はフォントサイズ倍率。
	// outlineThickness > 0 のとき、各グリフを 8 方向にオフセット複製してアウトラインを付ける。
	// 太いほどメモリ消費が増える（グリフ × 9 インスタンス）ので使いどころに注意。
	void DrawText(std::string_view utf8, const Vector2& pos, float scale = 1.0f,
		const Vector4& color = { 1.0f, 1.0f, 1.0f, 1.0f },
		float outlineThickness = 0.0f,
		const Vector4& outlineColor = { 0.0f, 0.0f, 0.0f, 1.0f });

	// 塗りつぶしの四角（線・枠・ゲージ用）。文字と同じ 1 DrawCall に入る
	void DrawRect(const Vector2& pos, const Vector2& size, const Vector4& color);

	// 発光の層に積む（ブルームの発光 RT に描く）。color は 1 を超えてよい（強さ）
	void DrawTextGlow(std::string_view utf8, const Vector2& pos, float scale, const Vector4& color);
	void DrawRectGlow(const Vector2& pos, const Vector2& size, const Vector4& color);

	// 以後に積む物の位置と大きさを center を中心に scale 倍する（ResetTransform まで。全部の層に効く）。
	// ブラウン管が切れるように UI を縦に潰す、などに使う
	void SetTransform(const Vector2& center, const Vector2& scale);
	void ResetTransform();

	// 文字列の描画ピクセル幅を取得（レイアウト用）。
	float MeasureWidth(std::string_view utf8, float scale = 1.0f);

	// フレームの描画の頭で呼ぶ（インスタンスバッファの書き込み位置を先頭へ戻す）。
	// 1 フレームに同じ層を何度 Flush しても、前の Flush の分を上書きしないよう続きへ書くため
	void BeginFrame();

	// シーンが描画を終えた最後に呼び出す。
	// 内部で bake 待ちをアトラスに反映し、最終的に 1 つの DrawCall を発行する。
	// 1 フレームに何度呼んでもよい（続きの位置へ書く。区画からあふれた分は描かない）
	void Flush();

	// 発光の層を、今バインドされている発光 RT（BloomEffect::kFormat）へ描く。Game の発光パスが呼ぶ
	bool HasPendingGlow() const { return !layers_[kLayerGlow].empty(); }
	void FlushGlow();

	// 画面の外（RenderTexture。sRGB の RGBA8）へ描く。BeginOffscreen の後の DrawText / DrawRect は
	// この層に積まれ、FlushOffscreen で今バインドされている RT へ描く（座標は RT のピクセル）
	void BeginOffscreen(float width, float height);
	void FlushOffscreen();

	FontAtlas* GetAtlas() { return atlas_.get(); }

	// 初期化済みか
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

	void CreateRootSignature();
	void CreatePipelineState();
	void CreateInstanceBuffer();

	void PushText(std::vector<GlyphInstance>& out, std::string_view utf8, const Vector2& pos, float scale,
		const Vector4& color, float outlineThickness, const Vector4& outlineColor);
	void PushRect(std::vector<GlyphInstance>& out, const Vector2& pos, const Vector2& size, const Vector4& color);
	// 通常の DrawText / DrawRect の積む先（BeginOffscreen の間は画面の外）
	std::vector<GlyphInstance>& CurrentLayer() { return layers_[offscreenActive_ ? kLayerOffscreen : kLayerScreen]; }
	// 層をインスタンスバッファの自分の区画へ写して描く
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
