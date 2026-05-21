#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
#include <wrl.h>
#include <d3d12.h>

class DirectXCore;
class SRVManager;

// 単一フォント・単一ピクセル高のグリフを R8 アトラスに焼く。
// ASCII+仮名はプリロード、他はオンデマンドで bake する。
class FontAtlas {
public:
	struct GlyphInfo {
		float u0 = 0.0f, v0 = 0.0f, u1 = 0.0f, v1 = 0.0f; // アトラス上の UV
		int   width = 0, height = 0;                       // ピクセル単位のグリフサイズ
		int   xoff = 0, yoff = 0;                          // ベースライン基準のオフセット
		float advance = 0.0f;                              // 次グリフへの進み（ピクセル）
		bool  valid = false;
	};

	// 初期化。ttfPath は AssetLocator::Open で読める形式（例 "Resources/Fonts/MPLUS1p-Medium.ttf"）。
	// pixelHeight は bake 解像度。atlasSize はテクスチャ一辺の画素数（正方）。
	bool Initialize(DirectXCore* dxCore, SRVManager* srvManager,
		const std::string& ttfPath, int pixelHeight, int atlasSize);

	void Finalize();

	// グリフを取得（未 bake なら bake してから返す）。失敗で valid=false が返る。
	const GlyphInfo& GetOrBake(uint32_t codepoint);

	// 蓄積した bake をアトラステクスチャに反映。フレーム描画前に呼ぶ。
	void FlushPendingUploads(ID3D12GraphicsCommandList* cmd);

	// アトラスを PIXEL_SHADER_RESOURCE 状態に置く（描画前バリア用）。
	void EnsureShaderResourceState(ID3D12GraphicsCommandList* cmd);

	uint32_t GetSrvIndex() const { return srvIndex_; }
	int      GetPixelHeight() const { return pixelHeight_; }
	int      GetAtlasSize() const { return atlasSize_; }
	float    GetAscent() const { return scaledAscent_; }
	float    GetDescent() const { return scaledDescent_; }
	float    GetLineGap() const { return scaledLineGap_; }
	float    GetLineAdvance() const { return scaledAscent_ - scaledDescent_ + scaledLineGap_; }

	// 既定文字セット（ASCII + 仮名）を bake してアトラスに反映する。
	// Initialize 直後に呼ぶ想定（フレームのコマンドリストが必要）。
	void PreloadDefaultCharset(ID3D12GraphicsCommandList* cmd);

private:
	DirectXCore* dxCore_ = nullptr;
	SRVManager* srvManager_ = nullptr;

	std::vector<uint8_t> ttfData_; // TTF 全データ
	struct FontInfoStorage;
	FontInfoStorage* fontInfo_ = nullptr; // pImpl 風（stb_truetype 型を隠す）

	int pixelHeight_ = 32;
	int atlasSize_ = 1024;
	float fontScale_ = 1.0f;
	float scaledAscent_ = 0.0f;
	float scaledDescent_ = 0.0f;
	float scaledLineGap_ = 0.0f;

	// シェルフパッキング用カーソル
	int shelfX_ = 1; // 0 番目の列は黒画素として空けておく（UV 端の補間防止）
	int shelfY_ = 1;
	int shelfHeight_ = 0;
	static constexpr int kShelfPadding = 1; // 隣接グリフのにじみ防止

	Microsoft::WRL::ComPtr<ID3D12Resource> atlasResource_;
	uint32_t srvIndex_ = 0;
	D3D12_RESOURCE_STATES currentState_ = D3D12_RESOURCE_STATE_COMMON;

	// bake 待ち
	struct PendingUpload {
		int x = 0, y = 0, w = 0, h = 0;
		std::vector<uint8_t> bytes; // R8 ピクセル
	};
	std::vector<PendingUpload> pending_;

	std::unordered_map<uint32_t, GlyphInfo> glyphs_;

	bool BakeGlyph(uint32_t codepoint, GlyphInfo& out);
	void TransitionTo(ID3D12GraphicsCommandList* cmd, D3D12_RESOURCE_STATES newState);
};
