#pragma once
#include <cstdint>
#include <string_view>

namespace Utf8 {

	// これ未満の先頭バイトは 1 バイト（ASCII）
	inline constexpr int kAsciiLimit = 0x80;
	// 続きのバイト（10xxxxxx）を見分けるマスクと印、中身のマスクとビット数
	inline constexpr int kContinuationMask = 0xC0;
	inline constexpr int kContinuationPrefix = 0x80;
	inline constexpr int kContinuationPayloadMask = 0x3F;
	inline constexpr int kContinuationBits = 6;
	// 2 バイトの先頭（110xxxxx）を見分けるマスクと印、中身のマスク、バイト数
	inline constexpr int kTwoByteLeadMask = 0xE0;
	inline constexpr int kTwoByteLeadPrefix = 0xC0;
	inline constexpr int kTwoBytePayloadMask = 0x1F;
	inline constexpr int kTwoByteLength = 2;
	// 3 バイトの先頭（1110xxxx）を見分けるマスクと印、中身のマスク、バイト数
	inline constexpr int kThreeByteLeadMask = 0xF0;
	inline constexpr int kThreeByteLeadPrefix = 0xE0;
	inline constexpr int kThreeBytePayloadMask = 0x0F;
	inline constexpr int kThreeByteLength = 3;
	// 4 バイトの先頭（11110xxx）を見分けるマスクと印、中身のマスク、バイト数
	inline constexpr int kFourByteLeadMask = 0xF8;
	inline constexpr int kFourByteLeadPrefix = 0xF0;
	inline constexpr int kFourBytePayloadMask = 0x07;
	inline constexpr int kFourByteLength = 4;
	// 不正なバイト列の代わりに返す文字（U+FFFD REPLACEMENT CHARACTER）
	inline constexpr int kReplacementCharacter = 0xFFFD;

	/// <summary>
	/// UTF-8 の 1 文字を読み出してコードポイントを返し、pos を次の文字の位置に進める。
	/// 不正バイト列の場合は 0xFFFD（U+REPLACEMENT CHARACTER）を返して 1 byte 進める
	/// </summary>
	/// <param name="s">UTF-8 の文字列</param>
	/// <param name="pos">読む位置 [byte]。呼び出し後は次の文字の位置</param>
	/// <returns>コードポイント（pos が終わりなら 0 を返し、pos は進めない）</returns>
	inline uint32_t DecodeNext(std::string_view s, size_t& pos)
	{
		if (pos >= s.size()) return 0;
		const unsigned char b0 = static_cast<unsigned char>(s[pos]);

		// 1 byte (ASCII)
		if (b0 < kAsciiLimit) {
			++pos;
			return b0;
		}

		auto cont = [&](size_t i, uint32_t& out) -> bool {
			if (i >= s.size()) return false;
			unsigned char b = static_cast<unsigned char>(s[i]);
			if ((b & kContinuationMask) != kContinuationPrefix) return false;
			out = (out << kContinuationBits) | (b & kContinuationPayloadMask);
			return true;
		};

		// 2 byte (0xC0-0xDF) -> 5 bit + 6 bit
		if ((b0 & kTwoByteLeadMask) == kTwoByteLeadPrefix) {
			uint32_t cp = b0 & kTwoBytePayloadMask;
			if (cont(pos + 1, cp)) { pos += kTwoByteLength; return cp; }
			++pos; return kReplacementCharacter;
		}

		// 3 byte (0xE0-0xEF) -> 4 bit + 6 + 6
		if ((b0 & kThreeByteLeadMask) == kThreeByteLeadPrefix) {
			uint32_t cp = b0 & kThreeBytePayloadMask;
			if (cont(pos + 1, cp) && cont(pos + 2, cp)) { pos += kThreeByteLength; return cp; }
			++pos; return kReplacementCharacter;
		}

		// 4 byte (0xF0-0xF7) -> 3 bit + 6 + 6 + 6
		if ((b0 & kFourByteLeadMask) == kFourByteLeadPrefix) {
			uint32_t cp = b0 & kFourBytePayloadMask;
			if (cont(pos + 1, cp) && cont(pos + 2, cp) && cont(pos + 3, cp)) {
				pos += kFourByteLength; return cp;
			}
			++pos; return kReplacementCharacter;
		}

		// 単独の継続バイト等は不正
		++pos;
		return kReplacementCharacter;
	}

} // namespace Utf8
