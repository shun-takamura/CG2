#pragma once

#include <string>

#include "JsonValue.h"

/// <summary>
/// JSONシリアライザ
/// pretty出力（インデント付き）とcompact出力（空白なし）に対応
/// コメントは出力しない（純JSON準拠）
/// </summary>
class JsonWriter {
public:
	struct Options {
		bool pretty = true;     // インデントと改行を入れるか
		int indentWidth = 2;    // pretty時の1段あたりのスペース数
	};

	/// <summary>
	/// 文字列にシリアライズ
	/// </summary>
	static std::string Write(const JsonValue& value, const Options& options = {});

	/// <summary>
	/// ファイルに書き出し
	/// </summary>
	static bool WriteFile(const std::string& filePath, const JsonValue& value, const Options& options = {});
};
