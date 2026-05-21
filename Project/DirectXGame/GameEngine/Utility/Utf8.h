#pragma once
#include <cstdint>
#include <string_view>

namespace Utf8 {

	// UTF-8 1 文字を読み出してコードポイントを返す。
	// pos は呼び出し後に次の文字の位置に進める。
	// 不正バイト列の場合は 0xFFFD（U+REPLACEMENT CHARACTER）を返して 1 byte 進める。
	inline uint32_t DecodeNext(std::string_view s, size_t& pos)
	{
		if (pos >= s.size()) return 0;
		const unsigned char b0 = static_cast<unsigned char>(s[pos]);

		// 1 byte (ASCII)
		if (b0 < 0x80) {
			++pos;
			return b0;
		}

		auto cont = [&](size_t i, uint32_t& out) -> bool {
			if (i >= s.size()) return false;
			unsigned char b = static_cast<unsigned char>(s[i]);
			if ((b & 0xC0) != 0x80) return false;
			out = (out << 6) | (b & 0x3F);
			return true;
		};

		// 2 byte (0xC0-0xDF) -> 5 bit + 6 bit
		if ((b0 & 0xE0) == 0xC0) {
			uint32_t cp = b0 & 0x1F;
			if (cont(pos + 1, cp)) { pos += 2; return cp; }
			++pos; return 0xFFFD;
		}

		// 3 byte (0xE0-0xEF) -> 4 bit + 6 + 6
		if ((b0 & 0xF0) == 0xE0) {
			uint32_t cp = b0 & 0x0F;
			if (cont(pos + 1, cp) && cont(pos + 2, cp)) { pos += 3; return cp; }
			++pos; return 0xFFFD;
		}

		// 4 byte (0xF0-0xF7) -> 3 bit + 6 + 6 + 6
		if ((b0 & 0xF8) == 0xF0) {
			uint32_t cp = b0 & 0x07;
			if (cont(pos + 1, cp) && cont(pos + 2, cp) && cont(pos + 3, cp)) {
				pos += 4; return cp;
			}
			++pos; return 0xFFFD;
		}

		// 単独の継続バイト等は不正
		++pos;
		return 0xFFFD;
	}

} // namespace Utf8
