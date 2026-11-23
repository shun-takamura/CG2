#include "JsonWriter.h"

#include <cmath>
#include <cstdio>
#include <fstream>

namespace {

	// これ未満の文字は制御文字として \uXXXX で書く
	constexpr int kFirstPrintableChar = 0x20;
	// \uXXXX を書くバッファの大きさ [byte]
	constexpr int kUnicodeEscapeBufferSize = 8;
	// 数を文字列にするバッファの大きさ [byte]
	constexpr int kNumberBufferSize = 32;

	/// <summary>文字列を二重引用符で囲み、JSON のエスケープをして足す（0x20 未満の制御文字は \uXXXX）</summary>
	/// <param name="out">足す先</param>
	/// <param name="s">書く文字列（UTF-8 のまま書く）</param>
	void AppendEscapedString(std::string& out, const std::string& s) {
		out.push_back('"');
		for (char c : s) {
			switch (c) {
			case '"':  out += "\\\""; break;
			case '\\': out += "\\\\"; break;
			case '\b': out += "\\b";  break;
			case '\f': out += "\\f";  break;
			case '\n': out += "\\n";  break;
			case '\r': out += "\\r";  break;
			case '\t': out += "\\t";  break;
			default:
				if (static_cast<unsigned char>(c) < kFirstPrintableChar) {
					char buf[kUnicodeEscapeBufferSize];
					std::snprintf(buf, sizeof(buf), "\\u%04X", c);
					out += buf;
				} else {
					out.push_back(c);
				}
			}
		}
		out.push_back('"');
	}

	/// <summary>Double を往復しても値が変わらない 17 桁で足す（NaN / 無限大は null）</summary>
	/// <param name="out">足す先</param>
	/// <param name="value">書く値</param>
	void AppendNumber(std::string& out, double value) {
		if (std::isnan(value) || std::isinf(value)) {
			out += "null"; // JSON仕様ではNaN/Infは表現不能なのでnullに置換
			return;
		}
		char buf[kNumberBufferSize];
		std::snprintf(buf, sizeof(buf), "%.17g", value);
		out += buf;
	}

	/// <summary>深さ × 幅のスペースを足す</summary>
	/// <param name="out">足す先</param>
	/// <param name="depth">入れ子の深さ</param>
	/// <param name="width">1 段あたりのスペース数</param>
	void AppendIndent(std::string& out, int depth, int width) {
		out.append(static_cast<size_t>(depth * width), ' ');
	}

	/// <summary>値を種類に合わせて書く（配列・オブジェクトは再帰する）</summary>
	/// <param name="out">足す先</param>
	/// <param name="value">書く値</param>
	/// <param name="options">整形の設定</param>
	/// <param name="depth">今の入れ子の深さ</param>
	void WriteValue(std::string& out, const JsonValue& value,
		const JsonWriter::Options& options, int depth);

	/// <summary>配列を書く（空なら []）</summary>
	/// <param name="out">足す先</param>
	/// <param name="arr">書く配列</param>
	/// <param name="options">整形の設定</param>
	/// <param name="depth">今の入れ子の深さ</param>
	void WriteArray(std::string& out, const JsonValue::Array& arr,
		const JsonWriter::Options& options, int depth) {
		if (arr.empty()) {
			out += "[]";
			return;
		}
		out.push_back('[');
		for (size_t i = 0; i < arr.size(); ++i) {
			if (options.pretty) {
				out.push_back('\n');
				AppendIndent(out, depth + 1, options.indentWidth);
			}
			WriteValue(out, arr[i], options, depth + 1);
			if (i + 1 < arr.size()) {
				out.push_back(',');
				if (!options.pretty) out.push_back(' ');
			}
		}
		if (options.pretty) {
			out.push_back('\n');
			AppendIndent(out, depth, options.indentWidth);
		}
		out.push_back(']');
	}

	/// <summary>オブジェクトを挿入順に書く（空なら {}）</summary>
	/// <param name="out">足す先</param>
	/// <param name="obj">書くオブジェクト</param>
	/// <param name="options">整形の設定</param>
	/// <param name="depth">今の入れ子の深さ</param>
	void WriteObject(std::string& out, const JsonValue::Object& obj,
		const JsonWriter::Options& options, int depth) {
		if (obj.empty()) {
			out += "{}";
			return;
		}
		out.push_back('{');
		for (size_t i = 0; i < obj.size(); ++i) {
			if (options.pretty) {
				out.push_back('\n');
				AppendIndent(out, depth + 1, options.indentWidth);
			}
			AppendEscapedString(out, obj[i].first);
			out.push_back(':');
			if (options.pretty) out.push_back(' ');
			WriteValue(out, obj[i].second, options, depth + 1);
			if (i + 1 < obj.size()) {
				out.push_back(',');
				if (!options.pretty) out.push_back(' ');
			}
		}
		if (options.pretty) {
			out.push_back('\n');
			AppendIndent(out, depth, options.indentWidth);
		}
		out.push_back('}');
	}

	void WriteValue(std::string& out, const JsonValue& value,
		const JsonWriter::Options& options, int depth) {
		switch (value.GetType()) {
		case JsonValue::Type::Null:
			out += "null";
			break;
		case JsonValue::Type::Bool:
			out += value.AsBool() ? "true" : "false";
			break;
		case JsonValue::Type::Int: {
			char buf[kNumberBufferSize];
			std::snprintf(buf, sizeof(buf), "%lld",
				static_cast<long long>(value.AsInt()));
			out += buf;
			break;
		}
		case JsonValue::Type::Double:
			AppendNumber(out, value.AsDouble());
			break;
		case JsonValue::Type::String:
			AppendEscapedString(out, value.AsString());
			break;
		case JsonValue::Type::Array:
			WriteArray(out, value.AsArray(), options, depth);
			break;
		case JsonValue::Type::Object:
			WriteObject(out, value.AsObject(), options, depth);
			break;
		}
	}

} // namespace

std::string JsonWriter::Write(const JsonValue& value, const Options& options) {
	std::string out;
	WriteValue(out, value, options, 0);
	return out;
}

bool JsonWriter::WriteFile(const std::string& filePath, const JsonValue& value, const Options& options) {
	std::ofstream ofs(filePath, std::ios::binary);
	if (!ofs) return false;
	std::string text = Write(value, options);
	ofs.write(text.data(), static_cast<std::streamsize>(text.size()));
	return ofs.good();
}
