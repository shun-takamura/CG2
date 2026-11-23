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
	/// </summary>
	/// <param name="filePath">レベルファイル（.json）のパス</param>
	/// <returns>読み込んだデータ（ファイルを開けない・JSON が壊れている・ルートが name="scene" のオブジェクトでない場合は nullptr）</returns>
	static std::unique_ptr<LevelData> Load(const std::string& filePath);

private:
	/// <summary>
	/// JSONのオブジェクトノード1個を ObjectData に変換する（子を再帰的に走査）
	/// </summary>
	/// <param name="jsonObject">オブジェクトのノード</param>
	/// <returns>変換したデータ（無い項目は既定値のまま）</returns>
	static LevelData::ObjectData ParseObject(const JsonValue& jsonObject);
};
