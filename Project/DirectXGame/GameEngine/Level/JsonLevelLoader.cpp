#include "JsonLevelLoader.h"

#include <cassert>

#include "Json/JsonParser.h"
#include "Json/JsonValue.h"
#include "Log.h"

std::unique_ptr<LevelData> JsonLevelLoader::Load(const std::string& filePath) {

	// ファイルを読み込んでパース
	JsonParser::Result result = JsonParser::ParseFile(filePath);
	if (!result.success) {
		Log("JsonLevelLoader: パースに失敗しました: " + filePath + " : " + result.errorMessage);
		return nullptr;
	}

	const JsonValue& root = result.value;

	// 正しいレベルデータファイルかチェック
	// ルートはオブジェクトで、"name" が文字列であること
	if (!root.IsObject() || !root.Contains("name") || !root["name"].IsString()) {
		Log("JsonLevelLoader: レベルデータの形式が不正です: " + filePath);
		return nullptr;
	}
	// 先頭ノード名が "scene" でなければ不正なレベルデータファイル
	if (root["name"].AsString() != "scene") {
		Log("JsonLevelLoader: ルートノード名が \"scene\" ではありません: " + filePath);
		return nullptr;
	}

	// レベルデータ格納用インスタンスを生成
	auto levelData = std::make_unique<LevelData>();
	levelData->name = root["name"].AsString();

	// "objects" の全オブジェクトを走査
	const JsonValue::Array& objects = root["objects"].AsArray();
	// reserve しておかないと push_back のたびに再確保が発生しうる
	levelData->objects.reserve(objects.size());
	for (const JsonValue& object : objects) {
		// 各オブジェクトには必ず "type" が入っている
		assert(object.Contains("type"));
		levelData->objects.push_back(ParseObject(object));
	}

	return levelData;
}

LevelData::ObjectData JsonLevelLoader::ParseObject(const JsonValue& jsonObject) {

	LevelData::ObjectData objectData;

	// 種類・名前
	objectData.type = jsonObject["type"].AsString();
	objectData.name = jsonObject["name"].AsString();

	// トランスフォーム読み込み（Blender → ゲームの座標系を吸収）
	//   Blender: X=右, Y=奥, Z=上 / 右手系
	//   ゲーム:   X=右, Y=上, Z=奥 / 左手系
	//   → 平行移動・スケールは Y と Z を入れ替え、回転はさらに符号を反転する
	if (jsonObject.Contains("transform")) {
		const JsonValue& transform = jsonObject["transform"];

		// 平行移動
		const JsonValue& t = transform["translation"];
		objectData.translation.x = static_cast<float>(t[0].AsDouble());
		objectData.translation.y = static_cast<float>(t[2].AsDouble());
		objectData.translation.z = static_cast<float>(t[1].AsDouble());

		// 回転角（度数法）
		const JsonValue& r = transform["rotation"];
		objectData.rotation.x = -static_cast<float>(r[0].AsDouble());
		objectData.rotation.y = -static_cast<float>(r[2].AsDouble());
		objectData.rotation.z = -static_cast<float>(r[1].AsDouble());

		// スケーリング
		const JsonValue& s = transform["scaling"];
		objectData.scaling.x = static_cast<float>(s[0].AsDouble());
		objectData.scaling.y = static_cast<float>(s[2].AsDouble());
		objectData.scaling.z = static_cast<float>(s[1].AsDouble());
	}

	// カスタムプロパティ file_name
	if (jsonObject.Contains("file_name")) {
		objectData.fileName = jsonObject["file_name"].AsString();
	}

	// カスタムプロパティ collider（座標系変換はトランスフォームと同じく Y/Z 入れ替え）
	if (jsonObject.Contains("collider")) {
		const JsonValue& collider = jsonObject["collider"];
		objectData.hasCollider = true;
		objectData.colliderType = collider["type"].AsString();

		const JsonValue& center = collider["center"];
		objectData.colliderCenter.x = static_cast<float>(center[0].AsDouble());
		objectData.colliderCenter.y = static_cast<float>(center[2].AsDouble());
		objectData.colliderCenter.z = static_cast<float>(center[1].AsDouble());

		const JsonValue& size = collider["size"];
		objectData.colliderSize.x = static_cast<float>(size[0].AsDouble());
		objectData.colliderSize.y = static_cast<float>(size[2].AsDouble());
		objectData.colliderSize.z = static_cast<float>(size[1].AsDouble());
	}

	// 子ノードがあれば再帰的に読み込み
	if (jsonObject.Contains("children")) {
		const JsonValue::Array& children = jsonObject["children"].AsArray();
		objectData.children.reserve(children.size());
		for (const JsonValue& child : children) {
			objectData.children.push_back(ParseObject(child));
		}
	}

	return objectData;
}
