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

	/// <summary>JSON 文字列をパースする。失敗しても例外は投げず、Result に失敗の内容を入れて返す</summary>
	/// <param name="source">JSON 文字列（UTF-8。// 行コメントとブロックコメントを含んでよい）</param>
	/// <returns>
	/// 成功なら success=true と value。失敗なら success=false、value は Null、
	/// errorMessage と、失敗を見つけた位置の errorLine / errorColumn（1 始まり）
	/// </returns>
	static Result Parse(std::string_view source);

	/// <summary>ファイルを丸ごと読み込んで Parse する</summary>
	/// <param name="filePath">読み込むファイルのパス</param>
	/// <returns>
	/// Parse の結果。ファイルを開けなかった時は success=false、errorMessage にパス入りの理由、
	/// errorLine / errorColumn は 0
	/// </returns>
	static Result ParseFile(const std::string& filePath);
};
