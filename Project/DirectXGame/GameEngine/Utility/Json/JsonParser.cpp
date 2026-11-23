#include "JsonParser.h"

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace {

	// UTF-16 のサロゲート（\u の値が D800〜DFFF の物。上位と下位の 2 つ組で 1 文字を表す。絵文字など）の範囲
	constexpr uint32_t kHighSurrogateFirst = 0xD800;
	constexpr uint32_t kHighSurrogateLast  = 0xDBFF;
	constexpr uint32_t kLowSurrogateFirst  = 0xDC00;
	constexpr uint32_t kLowSurrogateLast   = 0xDFFF;
	// サロゲートの組から求めるコードポイントの始まりと、上位・下位それぞれが持つビット数
	constexpr uint32_t kSupplementaryPlaneFirst = 0x10000;
	constexpr uint32_t kSurrogateBits = 10;

	// UTF-8 の 1〜4 バイトで表せる上限（この値未満なら、そのバイト数で収まる）
	constexpr uint32_t kUtf8OneByteLimit   = 0x80;
	constexpr uint32_t kUtf8TwoByteLimit   = 0x800;
	constexpr uint32_t kUtf8ThreeByteLimit = 0x10000;
	// UTF-8 の先頭バイトの印と、続きのバイト（10xxxxxx）の印・中身の取り出し
	constexpr uint32_t kUtf8TwoBytePrefix   = 0xC0;
	constexpr uint32_t kUtf8ThreeBytePrefix = 0xE0;
	constexpr uint32_t kUtf8FourBytePrefix  = 0xF0;
	constexpr uint32_t kUtf8ContinuationPrefix = 0x80;
	constexpr uint32_t kUtf8ContinuationMask   = 0x3F;
	// 続きのバイト 1 つが持つビット数
	constexpr uint32_t kUtf8ContinuationBits = 6;

	// \uXXXX の 16 進の桁数と、1 桁のビット数
	constexpr int kUnicodeEscapeDigits = 4;
	constexpr uint32_t kHexDigitBits = 4;
	constexpr uint32_t kHexLetterOffset = 10;

	// true / false / null の文字数
	constexpr int kTrueLiteralLength = 4;
	constexpr int kFalseLiteralLength = 5;
	constexpr int kNullLiteralLength = 4;
	// 整数を読むときの基数（10 進数）
	constexpr int kDecimalBase = 10;

	// パース失敗時の例外（内部用）
	struct ParseError {
		std::string message;
	};

	/// <summary>
	/// 再帰下降の JSON パーサ本体。失敗は ParseError を投げて JsonParser::Parse で受け止める。
	/// 読んでいる位置の行・列（1 始まり）を数えておき、失敗の位置として返す
	/// </summary>
	class Parser {
	public:
		/// <summary>パースする文字列を受け取る（文字列はパースが終わるまで生きている必要がある）</summary>
		/// <param name="src">パースする文字列</param>
		Parser(std::string_view src) : src_(src) {}

		/// <summary>ルートの値を 1 つ読み、後ろに空白・コメント以外が残っていないか確かめる</summary>
		/// <returns>ルートの値（失敗時は ParseError を投げる）</returns>
		JsonValue ParseRoot() {
			SkipWhitespaceAndComments();
			JsonValue value = ParseValue();
			SkipWhitespaceAndComments();
			if (!AtEnd()) {
				Throw("Trailing data after root JSON value");
			}
			return value;
		}

		/// <summary>今読んでいる位置の行</summary>
		/// <returns>行（1 始まり）</returns>
		size_t Line() const { return line_; }
		/// <summary>今読んでいる位置の列</summary>
		/// <returns>列（1 始まり、バイト単位）</returns>
		size_t Column() const { return column_; }

	private:
		/// <summary>文字列の終わりまで読んだか</summary>
		/// <returns>終わりなら true</returns>
		bool AtEnd() const { return pos_ >= src_.size(); }
		/// <summary>今の位置の文字を読み進めずに見る（終わりで呼んではいけない）</summary>
		/// <returns>今の位置の文字</returns>
		char Peek() const { return src_[pos_]; }

		/// <summary>1 文字読み進め、行・列を更新する（終わりで呼んではいけない）</summary>
		/// <returns>読んだ文字</returns>
		char Advance() {
			char c = src_[pos_++];
			if (c == '\n') {
				++line_;
				column_ = 1;
			} else {
				++column_;
			}
			return c;
		}

		/// <summary>今の位置が指定の文字なら読み進める</summary>
		/// <param name="c">期待する文字</param>
		/// <returns>一致して読み進めたら true</returns>
		bool Match(char c) {
			if (!AtEnd() && Peek() == c) {
				Advance();
				return true;
			}
			return false;
		}

		/// <summary>今の位置が指定の文字であることを確かめて読み進める（違えば ParseError を投げる）</summary>
		/// <param name="c">期待する文字</param>
		void Expect(char c) {
			if (AtEnd() || Peek() != c) {
				std::string msg = "Expected '";
				msg += c;
				msg += "'";
				Throw(msg);
			}
			Advance();
		}

		/// <summary>ParseError を投げる（戻らない）</summary>
		/// <param name="msg">失敗の理由</param>
		[[noreturn]] void Throw(const std::string& msg) {
			throw ParseError{ msg };
		}

		/// <summary>空白と、// 行コメント・ブロックコメントを読み飛ばす（閉じていないブロックコメントは終わりまで飛ばす）</summary>
		void SkipWhitespaceAndComments() {
			while (!AtEnd()) {
				char c = Peek();
				if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
					Advance();
				} else if (c == '/' && pos_ + 1 < src_.size()) {
					char next = src_[pos_ + 1];
					if (next == '/') {
						// 行コメント: 改行まで読み飛ばす
						Advance(); Advance();
						while (!AtEnd() && Peek() != '\n') {
							Advance();
						}
					} else if (next == '*') {
						// ブロックコメント: */ まで読み飛ばす
						Advance(); Advance();
						while (!AtEnd()) {
							if (Peek() == '*' && pos_ + 1 < src_.size() && src_[pos_ + 1] == '/') {
								Advance(); Advance();
								break;
							}
							Advance();
						}
					} else {
						return;
					}
				} else {
					return;
				}
			}
		}

		/// <summary>先頭の文字で種類を見分けて値を 1 つ読む</summary>
		/// <returns>読んだ値（入力の終わり・知らない文字なら ParseError を投げる）</returns>
		JsonValue ParseValue() {
			SkipWhitespaceAndComments();
			if (AtEnd()) Throw("Unexpected end of input");

			char c = Peek();
			if (c == '{') return ParseObject();
			if (c == '[') return ParseArray();
			if (c == '"') return JsonValue(ParseString());
			if (c == 't' || c == 'f') return ParseBool();
			if (c == 'n') return ParseNull();
			if (c == '-' || (c >= '0' && c <= '9')) return ParseNumber();
			Throw(std::string("Unexpected character '") + c + "'");
		}

		/// <summary>オブジェクト（{ "key": 値, ... }）を読む。挿入順を保ち、重複したキーもそのまま並べる</summary>
		/// <returns>Object 型の値（キーが文字列でない・区切りが違うと ParseError を投げる）</returns>
		JsonValue ParseObject() {
			Expect('{');
			JsonValue::Object obj;
			SkipWhitespaceAndComments();
			if (Match('}')) return JsonValue(std::move(obj));

			while (true) {
				SkipWhitespaceAndComments();
				if (AtEnd() || Peek() != '"') {
					Throw("Expected string key in object");
				}
				std::string key = ParseString();
				SkipWhitespaceAndComments();
				Expect(':');
				JsonValue value = ParseValue();
				obj.emplace_back(std::move(key), std::move(value));
				SkipWhitespaceAndComments();
				if (Match(',')) {
					continue;
				}
				if (Match('}')) {
					return JsonValue(std::move(obj));
				}
				Throw("Expected ',' or '}' in object");
			}
		}

		/// <summary>配列（[ 値, ... ]）を読む</summary>
		/// <returns>Array 型の値（区切りが違うと ParseError を投げる）</returns>
		JsonValue ParseArray() {
			Expect('[');
			JsonValue::Array arr;
			SkipWhitespaceAndComments();
			if (Match(']')) return JsonValue(std::move(arr));

			while (true) {
				arr.push_back(ParseValue());
				SkipWhitespaceAndComments();
				if (Match(',')) {
					continue;
				}
				if (Match(']')) {
					return JsonValue(std::move(arr));
				}
				Throw("Expected ',' or ']' in array");
			}
		}

		/// <summary>
		/// 文字列（"..."）を読み、エスケープを戻す。\uXXXX は UTF-8 にする（サロゲートペアは結合しない）
		/// </summary>
		/// <returns>中身の文字列（閉じていない・不正なエスケープは ParseError を投げる）</returns>
		std::string ParseString() {
			Expect('"');
			std::string out;
			while (!AtEnd()) {
				char c = Advance();
				if (c == '"') return out;
				if (c == '\\') {
					if (AtEnd()) Throw("Unterminated escape sequence");
					char esc = Advance();
					switch (esc) {
					case '"':  out.push_back('"');  break;
					case '\\': out.push_back('\\'); break;
					case '/':  out.push_back('/');  break;
					case 'b':  out.push_back('\b'); break;
					case 'f':  out.push_back('\f'); break;
					case 'n':  out.push_back('\n'); break;
					case 'r':  out.push_back('\r'); break;
					case 't':  out.push_back('\t'); break;
					case 'u': {
						// \uXXXX → UTF-8変換
						uint32_t codepoint = ReadUnicodeEscapeDigits();
						if (codepoint >= kHighSurrogateFirst && codepoint <= kHighSurrogateLast) {
							// 上位サロゲートの直後には \u で下位サロゲートが続く（2 つ組で 1 文字）
							if (AtEnd() || Advance() != '\\' || AtEnd() || Advance() != 'u') {
								Throw("High surrogate must be followed by \\u low surrogate");
							}
							const uint32_t low = ReadUnicodeEscapeDigits();
							if (low < kLowSurrogateFirst || low > kLowSurrogateLast) {
								Throw("Invalid low surrogate in \\u escape");
							}
							codepoint = kSupplementaryPlaneFirst
								+ ((codepoint - kHighSurrogateFirst) << kSurrogateBits)
								+ (low - kLowSurrogateFirst);
						} else if (codepoint >= kLowSurrogateFirst && codepoint <= kLowSurrogateLast) {
							// 上位サロゲート無しの下位サロゲートは 1 文字にならない
							Throw("Unpaired low surrogate in \\u escape");
						}
						AppendUtf8(out, codepoint);
						break;
					}
					default:
						Throw(std::string("Invalid escape '\\") + esc + "'");
					}
				} else {
					out.push_back(c);
				}
			}
			Throw("Unterminated string");
		}

		/// <summary>\u の後ろの 16 進 4 桁を読む</summary>
		/// <returns>読んだ値（0〜0xFFFF。足りない・16 進でない桁は ParseError を投げる）</returns>
		uint32_t ReadUnicodeEscapeDigits() {
			if (pos_ + kUnicodeEscapeDigits > src_.size()) Throw("Invalid \\u escape");
			uint32_t value = 0;
			for (int i = 0; i < kUnicodeEscapeDigits; ++i) {
				const char h = Advance();
				value <<= kHexDigitBits;
				if (h >= '0' && h <= '9') value |= static_cast<uint32_t>(h - '0');
				else if (h >= 'a' && h <= 'f') value |= static_cast<uint32_t>(h - 'a') + kHexLetterOffset;
				else if (h >= 'A' && h <= 'F') value |= static_cast<uint32_t>(h - 'A') + kHexLetterOffset;
				else Throw("Invalid hex digit in \\u escape");
			}
			return value;
		}

		/// <summary>コードポイントを UTF-8 の 1〜4 バイトにして足す</summary>
		/// <param name="out">足す先</param>
		/// <param name="cp">コードポイント（サロゲートの組は 1 つにまとめてから渡す）</param>
		static void AppendUtf8(std::string& out, uint32_t cp) {
			// 続きのバイト（10xxxxxx）を、下から shift ビット目の 6 ビットで作る
			auto continuation = [cp](uint32_t shift) {
				return static_cast<char>(kUtf8ContinuationPrefix | ((cp >> shift) & kUtf8ContinuationMask));
			};
			if (cp < kUtf8OneByteLimit) {
				out.push_back(static_cast<char>(cp));
			} else if (cp < kUtf8TwoByteLimit) {
				out.push_back(static_cast<char>(kUtf8TwoBytePrefix | (cp >> kUtf8ContinuationBits)));
				out.push_back(continuation(0));
			} else if (cp < kUtf8ThreeByteLimit) {
				out.push_back(static_cast<char>(kUtf8ThreeBytePrefix | (cp >> (kUtf8ContinuationBits * 2))));
				out.push_back(continuation(kUtf8ContinuationBits));
				out.push_back(continuation(0));
			} else {
				out.push_back(static_cast<char>(kUtf8FourBytePrefix | (cp >> (kUtf8ContinuationBits * 3))));
				out.push_back(continuation(kUtf8ContinuationBits * 2));
				out.push_back(continuation(kUtf8ContinuationBits));
				out.push_back(continuation(0));
			}
		}

		/// <summary>true / false を読む</summary>
		/// <returns>Bool 型の値（どちらでもなければ ParseError を投げる）</returns>
		JsonValue ParseBool() {
			if (src_.compare(pos_, kTrueLiteralLength, "true") == 0) {
				for (int i = 0; i < kTrueLiteralLength; ++i) Advance();
				return JsonValue(true);
			}
			if (src_.compare(pos_, kFalseLiteralLength, "false") == 0) {
				for (int i = 0; i < kFalseLiteralLength; ++i) Advance();
				return JsonValue(false);
			}
			Throw("Invalid literal (expected true/false)");
		}

		/// <summary>null を読む</summary>
		/// <returns>Null 型の値（null でなければ ParseError を投げる）</returns>
		JsonValue ParseNull() {
			if (src_.compare(pos_, kNullLiteralLength, "null") == 0) {
				for (int i = 0; i < kNullLiteralLength; ++i) Advance();
				return JsonValue(nullptr);
			}
			Throw("Invalid literal (expected null)");
		}

		/// <summary>今の位置から続く数字を読み飛ばす</summary>
		/// <returns>読み飛ばした数字の個数</returns>
		size_t SkipDigits() {
			size_t count = 0;
			while (!AtEnd() && std::isdigit(static_cast<unsigned char>(Peek()))) {
				Advance();
				++count;
			}
			return count;
		}

		/// <summary>
		/// 数値を読む。小数点か指数があれば Double、無ければ Int（int64）にする。
		/// 符号の後・小数点の後・指数の後に数字が無ければ ParseError を投げる
		/// </summary>
		/// <returns>Int または Double 型の値</returns>
		JsonValue ParseNumber() {
			size_t start = pos_;
			bool isFloat = false;

			if (Peek() == '-') Advance();

			// 整数部（"-" だけ・"-." のように数字が無いのはエラー）
			if (SkipDigits() == 0) Throw("Invalid number: digits expected");
			if (!AtEnd() && Peek() == '.') {
				isFloat = true;
				Advance();
				if (SkipDigits() == 0) Throw("Invalid number: digits expected after '.'");
			}
			if (!AtEnd() && (Peek() == 'e' || Peek() == 'E')) {
				isFloat = true;
				Advance();
				if (!AtEnd() && (Peek() == '+' || Peek() == '-')) Advance();
				if (SkipDigits() == 0) Throw("Invalid number: digits expected in exponent");
			}

			std::string numStr(src_.substr(start, pos_ - start));
			if (isFloat) {
				return JsonValue(std::strtod(numStr.c_str(), nullptr));
			}
			return JsonValue(static_cast<int64_t>(std::strtoll(numStr.c_str(), nullptr, kDecimalBase)));
		}

	private:
		std::string_view src_;
		size_t pos_ = 0;
		size_t line_ = 1;
		size_t column_ = 1;
	};

} // namespace

JsonParser::Result JsonParser::Parse(std::string_view source) {
	Result result;
	Parser parser(source);
	try {
		result.value = parser.ParseRoot();
		result.success = true;
	} catch (const ParseError& e) {
		result.success = false;
		result.errorMessage = e.message;
		result.errorLine = parser.Line();
		result.errorColumn = parser.Column();
	}
	return result;
}

JsonParser::Result JsonParser::ParseFile(const std::string& filePath) {
	std::ifstream ifs(filePath, std::ios::binary);
	if (!ifs) {
		Result result;
		result.success = false;
		result.errorMessage = "Failed to open file: " + filePath;
		return result;
	}
	std::ostringstream oss;
	oss << ifs.rdbuf();
	return Parse(oss.str());
}
