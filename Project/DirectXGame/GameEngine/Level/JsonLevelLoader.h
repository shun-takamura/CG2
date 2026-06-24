#pragma once

#include <memory>
#include <string>

#include "LevelData.h"

class JsonValue;

/// <summary>
/// 自作JSONライブラリ（JsonParser / JsonValue）を使った、
/// Blenderレベルエディタ出力(.json)の読み込み。
/// nlohmann::json などの外部ライブラリは使わない。
/// </summary>
class JsonLevelLoader {
public:
	/// <summary>
	/// JSONレベルファイルを読み込んで LevelData を構築する。
	/// 読み込み・フォーマット検証に失敗した場合は nullptr を返す。
	/// </summary>
	static std::unique_ptr<LevelData> Load(const std::string& filePath);

private:
	/// <summary>
	/// JSONのオブジェクトノード1個を ObjectData に変換する（子を再帰的に走査）
	/// </summary>
	static LevelData::ObjectData ParseObject(const JsonValue& jsonObject);
};
