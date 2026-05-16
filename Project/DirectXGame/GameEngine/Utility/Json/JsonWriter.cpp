#include "JsonWriter.h"

#include <cmath>
#include <cstdio>
#include <fstream>

namespace {

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
				if (static_cast<unsigned char>(c) < 0x20) {
					char buf[8];
					std::snprintf(buf, sizeof(buf), "\\u%04X", c);
					out += buf;
				} else {
					out.push_back(c);
				}
			}
		}
		out.push_back('"');
	}

	void AppendNumber(std::string& out, double value) {
		if (std::isnan(value) || std::isinf(value)) {
			out += "null"; // JSON仕様ではNaN/Infは表現不能なのでnullに置換
			return;
		}
		char buf[32];
		std::snprintf(buf, sizeof(buf), "%.17g", value);
		out += buf;
	}

	void AppendIndent(std::string& out, int depth, int width) {
		out.append(static_cast<size_t>(depth * width), ' ');
	}

	void WriteValue(std::string& out, const JsonValue& value,
		const JsonWriter::Options& options, int depth);

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
			char buf[32];
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
