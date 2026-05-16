#include "JsonParser.h"

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace {

	// パース失敗時の例外（内部用）
	struct ParseError {
		std::string message;
	};

	class Parser {
	public:
		Parser(std::string_view src) : src_(src) {}

		JsonValue ParseRoot() {
			SkipWhitespaceAndComments();
			JsonValue value = ParseValue();
			SkipWhitespaceAndComments();
			if (!AtEnd()) {
				Throw("Trailing data after root JSON value");
			}
			return value;
		}

		size_t Line() const { return line_; }
		size_t Column() const { return column_; }

	private:
		bool AtEnd() const { return pos_ >= src_.size(); }
		char Peek() const { return src_[pos_]; }

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

		bool Match(char c) {
			if (!AtEnd() && Peek() == c) {
				Advance();
				return true;
			}
			return false;
		}

		void Expect(char c) {
			if (AtEnd() || Peek() != c) {
				std::string msg = "Expected '";
				msg += c;
				msg += "'";
				Throw(msg);
			}
			Advance();
		}

		[[noreturn]] void Throw(const std::string& msg) {
			throw ParseError{ msg };
		}

		// // と /* */ をスキップ
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
						if (pos_ + 4 > src_.size()) Throw("Invalid \\u escape");
						uint32_t codepoint = 0;
						for (int i = 0; i < 4; ++i) {
							char h = Advance();
							codepoint <<= 4;
							if (h >= '0' && h <= '9') codepoint |= (h - '0');
							else if (h >= 'a' && h <= 'f') codepoint |= (h - 'a' + 10);
							else if (h >= 'A' && h <= 'F') codepoint |= (h - 'A' + 10);
							else Throw("Invalid hex digit in \\u escape");
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

		static void AppendUtf8(std::string& out, uint32_t cp) {
			if (cp < 0x80) {
				out.push_back(static_cast<char>(cp));
			} else if (cp < 0x800) {
				out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
				out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
			} else {
				out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
				out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
				out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
			}
		}

		JsonValue ParseBool() {
			if (src_.compare(pos_, 4, "true") == 0) {
				for (int i = 0; i < 4; ++i) Advance();
				return JsonValue(true);
			}
			if (src_.compare(pos_, 5, "false") == 0) {
				for (int i = 0; i < 5; ++i) Advance();
				return JsonValue(false);
			}
			Throw("Invalid literal (expected true/false)");
		}

		JsonValue ParseNull() {
			if (src_.compare(pos_, 4, "null") == 0) {
				for (int i = 0; i < 4; ++i) Advance();
				return JsonValue(nullptr);
			}
			Throw("Invalid literal (expected null)");
		}

		JsonValue ParseNumber() {
			size_t start = pos_;
			bool isFloat = false;

			if (Peek() == '-') Advance();

			while (!AtEnd() && std::isdigit(static_cast<unsigned char>(Peek()))) {
				Advance();
			}
			if (!AtEnd() && Peek() == '.') {
				isFloat = true;
				Advance();
				while (!AtEnd() && std::isdigit(static_cast<unsigned char>(Peek()))) {
					Advance();
				}
			}
			if (!AtEnd() && (Peek() == 'e' || Peek() == 'E')) {
				isFloat = true;
				Advance();
				if (!AtEnd() && (Peek() == '+' || Peek() == '-')) Advance();
				while (!AtEnd() && std::isdigit(static_cast<unsigned char>(Peek()))) {
					Advance();
				}
			}

			std::string numStr(src_.substr(start, pos_ - start));
			if (isFloat) {
				return JsonValue(std::strtod(numStr.c_str(), nullptr));
			}
			return JsonValue(static_cast<int64_t>(std::strtoll(numStr.c_str(), nullptr, 10)));
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
