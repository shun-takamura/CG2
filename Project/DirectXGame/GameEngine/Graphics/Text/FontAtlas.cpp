#include "FontAtlas.h"
#include "DirectXCore.h"
#include "SRVManager.h"
#include "AssetLocator.h"
#include "Log.h"

#include "stb_truetype.h"

#include <cassert>
#include <cstring>
#include <string>

// stb_truetype の型を .h に露出させたくないため pImpl 化
struct FontAtlas::FontInfoStorage {
	stbtt_fontinfo info{};
};

namespace {
	// 行を計算するヘルパ：D3D12 の RowPitch は 256 byte 単位
	uint32_t AlignUp(uint32_t v, uint32_t a) { return (v + (a - 1)) & ~(a - 1); }
} // namespace

bool FontAtlas::Initialize(DirectXCore* dxCore, SRVManager* srvManager,
	const std::string& ttfPath, int pixelHeight, int atlasSize)
{
	dxCore_ = dxCore;
	srvManager_ = srvManager;
	pixelHeight_ = pixelHeight;
	atlasSize_ = atlasSize;

	// TTF 読み込み
	ttfData_ = AssetLocator::GetInstance()->LoadAll(ttfPath);
	if (ttfData_.empty()) {
		Log("[FontAtlas] failed to load TTF: " + ttfPath + "\n");
		return false;
	}

	// stb_truetype 初期化
	fontInfo_ = new FontInfoStorage();
	int fontOffset = stbtt_GetFontOffsetForIndex(ttfData_.data(), 0);
	if (!stbtt_InitFont(&fontInfo_->info, ttfData_.data(), fontOffset)) {
		Log("[FontAtlas] stbtt_InitFont failed: " + ttfPath + "\n");
		delete fontInfo_;
		fontInfo_ = nullptr;
		return false;
	}

	fontScale_ = stbtt_ScaleForPixelHeight(&fontInfo_->info, static_cast<float>(pixelHeight));
	int ascent = 0, descent = 0, lineGap = 0;
	stbtt_GetFontVMetrics(&fontInfo_->info, &ascent, &descent, &lineGap);
	scaledAscent_ = ascent * fontScale_;
	scaledDescent_ = descent * fontScale_;
	scaledLineGap_ = lineGap * fontScale_;

	// アトラステクスチャ（R8_UNORM, DEFAULT heap）
	D3D12_HEAP_PROPERTIES heapProps{};
	heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

	D3D12_RESOURCE_DESC texDesc{};
	texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	texDesc.Width = static_cast<UINT64>(atlasSize_);
	texDesc.Height = static_cast<UINT>(atlasSize_);
	texDesc.DepthOrArraySize = 1;
	texDesc.MipLevels = 1;
	texDesc.Format = DXGI_FORMAT_R8_UNORM;
	texDesc.SampleDesc.Count = 1;
	texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	texDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

	HRESULT hr = dxCore_->GetDevice()->CreateCommittedResource(
		&heapProps, D3D12_HEAP_FLAG_NONE, &texDesc,
		D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
		IID_PPV_ARGS(&atlasResource_));
	if (FAILED(hr)) {
		Log("[FontAtlas] failed to create atlas texture\n");
		return false;
	}
	currentState_ = D3D12_RESOURCE_STATE_COPY_DEST;

	// SRV 確保
	srvIndex_ = srvManager_->Allocate();
	srvManager_->CreateSRVForTexture2D(srvIndex_, atlasResource_.Get(), DXGI_FORMAT_R8_UNORM, 1);

	// アトラス左上の (0,0) を黒（α=0）として 1 ピクセル分だけ初期アップロードしておく。
	// これでサンプリングで境界に触れても 0 が返る。
	{
		PendingUpload up;
		up.x = 0; up.y = 0; up.w = 1; up.h = 1;
		up.bytes.assign(1, 0);
		pending_.push_back(std::move(up));
	}

	return true;
}

void FontAtlas::Finalize()
{
	if (fontInfo_) {
		delete fontInfo_;
		fontInfo_ = nullptr;
	}
	atlasResource_.Reset();
	pending_.clear();
	glyphs_.clear();
	ttfData_.clear();
}

bool FontAtlas::BakeGlyph(uint32_t codepoint, GlyphInfo& out)
{
	if (!fontInfo_) return false;

	int glyphIndex = stbtt_FindGlyphIndex(&fontInfo_->info, static_cast<int>(codepoint));
	int adv = 0, lsb = 0;
	stbtt_GetGlyphHMetrics(&fontInfo_->info, glyphIndex, &adv, &lsb);
	out.advance = adv * fontScale_;

	if (glyphIndex == 0) {
		out.width = out.height = 0;
		out.u0 = out.v0 = out.u1 = out.v1 = 0.0f;
		out.xoff = out.yoff = 0;
		out.valid = (codepoint == 0x20);
		return true;
	}

	// stbtt_GetGlyphSDF：縁からの距離フィールドを焼く。
	// padding ぶんだけ周囲に余白が追加された (gw + 2*pad) x (gh + 2*pad) のビットマップが返る。
	// 値域は [0..255]、onedge_value が縁、+方向(内側)/-方向(外側) に pixel_dist_scale で距離が広がる。
	constexpr int   kSdfPadding       = 4;
	constexpr unsigned char kOnEdge   = 128;
	constexpr float kPixelDistScale   = 32.0f; // 128 / 4 = 32 → ±4 px 範囲を 0..255 にマップ

	int gw = 0, gh = 0;
	int xoff = 0, yoff = 0;
	unsigned char* sdf = stbtt_GetGlyphSDF(&fontInfo_->info, fontScale_, glyphIndex,
		kSdfPadding, kOnEdge, kPixelDistScale,
		&gw, &gh, &xoff, &yoff);

	if (!sdf || gw <= 0 || gh <= 0) {
		// 描画する画素なし（空白等）
		if (sdf) stbtt_FreeSDF(sdf, nullptr);
		out.width = out.height = 0;
		out.u0 = out.v0 = out.u1 = out.v1 = 0.0f;
		out.xoff = 0;
		out.yoff = 0;
		out.valid = true;
		return true;
	}

	out.xoff = xoff;
	out.yoff = yoff;
	out.width = gw;
	out.height = gh;

	// シェルフパッキング
	int padW = gw + kShelfPadding;
	int padH = gh + kShelfPadding;

	if (shelfX_ + padW > atlasSize_) {
		shelfY_ += shelfHeight_ + kShelfPadding;
		shelfX_ = 1;
		shelfHeight_ = 0;
	}
	if (shelfY_ + padH > atlasSize_) {
		Log("[FontAtlas] atlas full, glyph dropped: U+" + std::to_string(codepoint) + "\n");
		stbtt_FreeSDF(sdf, nullptr);
		out.valid = false;
		return false;
	}
	int px = shelfX_;
	int py = shelfY_;
	shelfX_ += padW;
	if (padH > shelfHeight_) shelfHeight_ = padH;

	// SDF を pending にコピー
	PendingUpload up;
	up.x = px;
	up.y = py;
	up.w = gw;
	up.h = gh;
	up.bytes.assign(sdf, sdf + static_cast<size_t>(gw) * gh);
	stbtt_FreeSDF(sdf, nullptr);

	out.u0 = static_cast<float>(px) / atlasSize_;
	out.v0 = static_cast<float>(py) / atlasSize_;
	out.u1 = static_cast<float>(px + gw) / atlasSize_;
	out.v1 = static_cast<float>(py + gh) / atlasSize_;
	out.valid = true;

	pending_.push_back(std::move(up));
	return true;
}

const FontAtlas::GlyphInfo& FontAtlas::GetOrBake(uint32_t codepoint)
{
	auto it = glyphs_.find(codepoint);
	if (it != glyphs_.end()) return it->second;

	GlyphInfo info{};
	BakeGlyph(codepoint, info);
	auto [ins, _] = glyphs_.emplace(codepoint, info);
	return ins->second;
}

void FontAtlas::TransitionTo(ID3D12GraphicsCommandList* cmd, D3D12_RESOURCE_STATES newState)
{
	if (currentState_ == newState) return;
	D3D12_RESOURCE_BARRIER barrier{};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Transition.pResource = atlasResource_.Get();
	barrier.Transition.StateBefore = currentState_;
	barrier.Transition.StateAfter = newState;
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	cmd->ResourceBarrier(1, &barrier);
	currentState_ = newState;
}

void FontAtlas::FlushPendingUploads(ID3D12GraphicsCommandList* cmd)
{
	if (pending_.empty()) return;
	TransitionTo(cmd, D3D12_RESOURCE_STATE_COPY_DEST);

	// 全 pending を 1 つの UPLOAD バッファに詰めて CopyTextureRegion を発行
	for (auto& up : pending_) {
		// 行 pitch を 256 にアラインしたバッファを作る
		const uint32_t rowPitch = AlignUp(static_cast<uint32_t>(up.w), D3D12_TEXTURE_DATA_PITCH_ALIGNMENT);
		const uint32_t bufSize = rowPitch * static_cast<uint32_t>(up.h);

		Microsoft::WRL::ComPtr<ID3D12Resource> upload = dxCore_->CreateBufferResource(bufSize);

		// マップしてコピー
		void* mapped = nullptr;
		upload->Map(0, nullptr, &mapped);
		uint8_t* dst = static_cast<uint8_t*>(mapped);
		for (int row = 0; row < up.h; ++row) {
			std::memcpy(dst + row * rowPitch, up.bytes.data() + row * up.w, up.w);
		}
		upload->Unmap(0, nullptr);

		// CopyTextureRegion
		D3D12_TEXTURE_COPY_LOCATION dstLoc{};
		dstLoc.pResource = atlasResource_.Get();
		dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
		dstLoc.SubresourceIndex = 0;

		D3D12_TEXTURE_COPY_LOCATION srcLoc{};
		srcLoc.pResource = upload.Get();
		srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
		srcLoc.PlacedFootprint.Offset = 0;
		srcLoc.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8_UNORM;
		srcLoc.PlacedFootprint.Footprint.Width = static_cast<UINT>(up.w);
		srcLoc.PlacedFootprint.Footprint.Height = static_cast<UINT>(up.h);
		srcLoc.PlacedFootprint.Footprint.Depth = 1;
		srcLoc.PlacedFootprint.Footprint.RowPitch = rowPitch;

		D3D12_BOX srcBox{};
		srcBox.left = 0; srcBox.top = 0; srcBox.front = 0;
		srcBox.right = static_cast<UINT>(up.w);
		srcBox.bottom = static_cast<UINT>(up.h);
		srcBox.back = 1;

		cmd->CopyTextureRegion(&dstLoc, static_cast<UINT>(up.x), static_cast<UINT>(up.y), 0, &srcLoc, &srcBox);

		// 中間バッファを GPU 完了まで保持
		dxCore_->TrackIntermediateResource(upload);
	}

	pending_.clear();
}

void FontAtlas::EnsureShaderResourceState(ID3D12GraphicsCommandList* cmd)
{
	TransitionTo(cmd, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
}

void FontAtlas::PreloadDefaultCharset(ID3D12GraphicsCommandList* /*cmd*/)
{
	// ASCII 印字可能領域
	for (uint32_t cp = 0x20; cp <= 0x7E; ++cp) GetOrBake(cp);
	// ひらがな (U+3040-U+309F)
	for (uint32_t cp = 0x3040; cp <= 0x309F; ++cp) GetOrBake(cp);
	// カタカナ (U+30A0-U+30FF)
	for (uint32_t cp = 0x30A0; cp <= 0x30FF; ++cp) GetOrBake(cp);
	// 全角記号でよく使う一部
	for (uint32_t cp : { 0x3001u, 0x3002u, 0x300Cu, 0x300Du, 0xFF01u, 0xFF1Fu, 0xFFE5u }) {
		GetOrBake(cp);
	}
	// cmd 経由の Flush は呼び出し側で行う（最初のフレームの TextRenderer::PreDraw 等）
}
