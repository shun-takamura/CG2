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

	/// <summary>値を JSON 文字列にする。NaN / 無限大の Double は null として書く</summary>
	/// <param name="value">書く値</param>
	/// <param name="options">整形の設定（既定は 2 スペースの pretty）</param>
	/// <returns>JSON 文字列</returns>
	static std::string Write(const JsonValue& value, const Options& options = {});

	/// <summary>値を JSON 文字列にしてファイルに書き出す（既存のファイルは上書き、改行は LF のまま）</summary>
	/// <param name="filePath">書き出すファイルのパス</param>
	/// <param name="value">書く値</param>
	/// <param name="options">整形の設定（既定は 2 スペースの pretty）</param>
	/// <returns>開けて書き終えたら true（開けない・書き込みに失敗したら false）</returns>
	static bool WriteFile(const std::string& filePath, const JsonValue& value, const Options& options = {});
};
