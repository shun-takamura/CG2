#pragma once
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

	// 文字列の描画ピクセル幅を取得（レイアウト用）。
	float MeasureWidth(std::string_view utf8, float scale = 1.0f);

	// シーンが描画を終えた最後に呼び出す。
	// 内部で bake 待ちをアトラスに反映し、最終的に 1 つの DrawCall を発行する。
	void Flush();

	FontAtlas* GetAtlas() { return atlas_.get(); }

	// 初期化済みか
	bool IsInitialized() const { return initialized_; }

private:
	TextRenderer() = default;
	~TextRenderer() = default;
	TextRenderer(const TextRenderer&) = delete;
	TextRenderer& operator=(const TextRenderer&) = delete;

	void CreateRootSignature();
	void CreatePipelineState();
	void CreateInstanceBuffer();
	void CreateScreenCB();

	DirectXCore* dxCore_ = nullptr;
	SRVManager* srvManager_ = nullptr;
	std::unique_ptr<FontAtlas> atlas_;

	bool initialized_ = false;

	Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState_;

	// インスタンス CPU/GPU バッファ。HLSL の GlyphInstance とレイアウト一致。
	struct GlyphInstance {
		float screenPosX, screenPosY;
		float sizeX, sizeY;
		float u0, v0, u1, v1;
		float r, g, b, a;
		float outR, outG, outB, outA;
		float outlineWidth;
		float _pad0, _pad1, _pad2;
	};
	static constexpr uint32_t kMaxInstances = 4096;

	Microsoft::WRL::ComPtr<ID3D12Resource> instanceResource_;
	GlyphInstance* instanceData_ = nullptr;
	uint32_t instanceSrvIndex_ = 0;

	std::vector<GlyphInstance> cpuInstances_; // フレーム内バッファ

	// 画面サイズ CBV
	struct ScreenCB { float screenW, screenH, pad0, pad1; };
	Microsoft::WRL::ComPtr<ID3D12Resource> screenCB_;
	ScreenCB* screenCBData_ = nullptr;
};
