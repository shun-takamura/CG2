#pragma once

#include <string>
#include <string_view>

#include "JsonValue.h"

/// <summary>
/// JSONパーサ
/// 純JSON仕様 + 拡張として // 行コメントと / * ブロックコメント * / を許容する
/// </summary>
class JsonParser {
public:
	struct Result {
		bool success = false;
		JsonValue value;
		std::string errorMessage;
		size_t errorLine = 0;
		size_t errorColumn = 0;
	};

	/// <summary>
	/// 文字列からパース
	/// </summary>
	static Result Parse(std::string_view source);

	/// <summary>
	/// ファイルから読み込んでパース
	/// </summary>
	static Result ParseFile(const std::string& filePath);
};
