#pragma once
#include <cstdint>
#include <cstring>
#include <dxgiformat.h>

// =====================================================================
// DDS ファイルヘッダー解析ユーティリティ
//
// 用途:
//   DirectStorage で ID3D12Resource を事前確保するため、ファイル先頭の
//   ヘッダー部分（148 バイトまで）だけを読んで width/height/format などを
//   抽出する。ペイロード（ピクセルデータ）はロードしない。
//
// 対応:
//   - 標準 DDS ヘッダー（旧フォーマット FourCC: DXT1/DXT3/DXT5/BC4/BC5）
//   - DDS_HEADER_DXT10 拡張（BC6H / BC7 / その他 DXGI_FORMAT 直接指定）
//   - キューブマップ判定（caps2 の CUBEMAP フラグ）
//
// 参考: https://learn.microsoft.com/windows/win32/direct3ddds/dx-graphics-dds-pguide
// =====================================================================
namespace DDS {

// "DDS " マジック
constexpr uint32_t kMagic = 0x20534444;

// ヘッダーサイズ定数
constexpr uint32_t kHeaderSize = 124;
constexpr uint32_t kPixelFormatSize = 32;
constexpr uint32_t kDXT10HeaderSize = 20;

// 旧フォーマット FourCC（DDS_PIXELFORMAT.dwFourCC に格納される）
constexpr uint32_t kFourCC_DXT1 = 0x31545844; // "DXT1"
constexpr uint32_t kFourCC_DXT3 = 0x33545844; // "DXT3"
constexpr uint32_t kFourCC_DXT5 = 0x35545844; // "DXT5"
constexpr uint32_t kFourCC_BC4U = 0x55344342; // "BC4U"
constexpr uint32_t kFourCC_BC4S = 0x53344342; // "BC4S"
constexpr uint32_t kFourCC_BC5U = 0x55354342; // "BC5U"
constexpr uint32_t kFourCC_BC5S = 0x53354342; // "BC5S"
constexpr uint32_t kFourCC_ATI1 = 0x31495441; // "ATI1" (BC4 互換)
constexpr uint32_t kFourCC_ATI2 = 0x32495441; // "ATI2" (BC5 互換)
constexpr uint32_t kFourCC_DX10 = 0x30315844; // "DX10" (DXT10 拡張)

// DDS_PIXELFORMAT.dwFlags
constexpr uint32_t kDDPF_FOURCC = 0x00000004;

// DDS_HEADER.dwCaps2（旧形式でのキューブマップ判定）
constexpr uint32_t kDDSCAPS2_CUBEMAP = 0x00000200;

// DDS_HEADER_DXT10.miscFlag（DX10 拡張形式でのキューブマップ判定）
constexpr uint32_t kDDS_RESOURCE_MISC_TEXTURECUBE = 0x00000004;

// DDS_PIXELFORMAT 構造（32 バイト）
struct PixelFormat {
	uint32_t size;
	uint32_t flags;
	uint32_t fourCC;
	uint32_t rgbBitCount;
	uint32_t rBitMask;
	uint32_t gBitMask;
	uint32_t bBitMask;
	uint32_t aBitMask;
};
static_assert(sizeof(PixelFormat) == kPixelFormatSize, "DDS PixelFormat must be 32 bytes");

// DDS_HEADER 構造（124 バイト）
struct Header {
	uint32_t size;
	uint32_t flags;
	uint32_t height;
	uint32_t width;
	uint32_t pitchOrLinearSize;
	uint32_t depth;
	uint32_t mipMapCount;
	uint32_t reserved1[11];
	PixelFormat ddspf;
	uint32_t caps;
	uint32_t caps2;
	uint32_t caps3;
	uint32_t caps4;
	uint32_t reserved2;
};
static_assert(sizeof(Header) == kHeaderSize, "DDS Header must be 124 bytes");

// DDS_HEADER_DXT10 拡張（20 バイト、FourCC が "DX10" のときだけ続く）
struct HeaderDXT10 {
	uint32_t dxgiFormat;        // DXGI_FORMAT を直値で格納
	uint32_t resourceDimension; // 2=1D, 3=2D, 4=3D
	uint32_t miscFlag;          // 0x4 ならキューブマップ
	uint32_t arraySize;
	uint32_t miscFlags2;
};
static_assert(sizeof(HeaderDXT10) == kDXT10HeaderSize, "DXT10 header must be 20 bytes");

// 解析結果（DirectStorage で ID3D12Resource を事前確保するのに必要な情報）
struct ParsedDDS {
	uint32_t    width = 0;
	uint32_t    height = 0;
	uint32_t    mipLevels = 1;
	uint32_t    arraySize = 1;          // キューブマップなら 6
	DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
	bool        isCubemap = false;
	size_t      payloadOffset = 0;       // ピクセルデータ開始バイト位置
};

// 旧フォーマット FourCC → DXGI_FORMAT 変換（DXT10 拡張がないケースの最小対応）
inline DXGI_FORMAT FourCCToFormat(uint32_t fourCC) {
	switch (fourCC) {
	case kFourCC_DXT1: return DXGI_FORMAT_BC1_UNORM;
	case kFourCC_DXT3: return DXGI_FORMAT_BC2_UNORM;
	case kFourCC_DXT5: return DXGI_FORMAT_BC3_UNORM;
	case kFourCC_BC4U:
	case kFourCC_ATI1: return DXGI_FORMAT_BC4_UNORM;
	case kFourCC_BC4S: return DXGI_FORMAT_BC4_SNORM;
	case kFourCC_BC5U:
	case kFourCC_ATI2: return DXGI_FORMAT_BC5_UNORM;
	case kFourCC_BC5S: return DXGI_FORMAT_BC5_SNORM;
	default:           return DXGI_FORMAT_UNKNOWN;
	}
}

// バイナリ先頭から DDS ヘッダーを解析する。
// data はファイル先頭、size は読み込めるバイト数。最低 4 + 124 バイト必要、
// DXT10 拡張があれば 4 + 124 + 20 バイト必要。
// 成功なら true、不正なフォーマット / バイト数不足なら false。
inline bool ParseDDS(const void* data, size_t size, ParsedDDS& out) {
	if (!data || size < 4 + kHeaderSize) return false;

	const auto* bytes = static_cast<const uint8_t*>(data);

	// マジック
	uint32_t magic = 0;
	std::memcpy(&magic, bytes, 4);
	if (magic != kMagic) return false;

	// メインヘッダー
	Header header{};
	std::memcpy(&header, bytes + 4, kHeaderSize);
	if (header.size != kHeaderSize) return false;
	if (header.ddspf.size != kPixelFormatSize) return false;

	out.width = header.width;
	out.height = header.height;
	out.mipLevels = (header.mipMapCount > 0) ? header.mipMapCount : 1;
	out.arraySize = 1;
	out.payloadOffset = 4 + kHeaderSize;

	// 旧形式でのキューブマップ判定（DX10 拡張がない場合）
	out.isCubemap = (header.caps2 & kDDSCAPS2_CUBEMAP) != 0;

	const bool hasFourCC = (header.ddspf.flags & kDDPF_FOURCC) != 0;
	const bool hasDXT10 = hasFourCC && header.ddspf.fourCC == kFourCC_DX10;

	if (hasDXT10) {
		if (size < 4 + kHeaderSize + kDXT10HeaderSize) return false;
		HeaderDXT10 dxt10{};
		std::memcpy(&dxt10, bytes + 4 + kHeaderSize, kDXT10HeaderSize);

		out.format = static_cast<DXGI_FORMAT>(dxt10.dxgiFormat);
		if (dxt10.arraySize > 0) out.arraySize = dxt10.arraySize;
		out.payloadOffset = 4 + kHeaderSize + kDXT10HeaderSize;

		// DX10 拡張ではキューブマップは miscFlag で判定（caps2 ではなく）
		if (dxt10.miscFlag & kDDS_RESOURCE_MISC_TEXTURECUBE) {
			out.isCubemap = true;
		}
	} else if (hasFourCC) {
		out.format = FourCCToFormat(header.ddspf.fourCC);
	} else {
		// 非圧縮 RGB/RGBA は今は未対応（必要になったら追加）
		out.format = DXGI_FORMAT_UNKNOWN;
	}

	// キューブマップは 1 個あたり 6 面のスライス扱い。
	// arraySize は「キューブマップの個数」なので、ID3D12Resource 作成時には
	// 実効的な subresource 数 = arraySize * 6 * mipLevels になる。
	if (out.isCubemap) {
		out.arraySize *= 6;
	}

	return true;
}

} // namespace DDS
