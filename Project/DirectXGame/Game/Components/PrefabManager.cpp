#include "PrefabManager.h"

#include "Json/JsonValue.h"
#include "Json/JsonParser.h"
#include "Json/JsonWriter.h"
#include "Log.h"

#include <filesystem>

namespace {
	constexpr const char* kPrefabDir = "Resources/Json/Prefabs";

	JsonValue Vec3ToJson(const Vector3& v) {
		JsonValue arr = JsonValue::MakeArray();
		arr.Push(JsonValue(static_cast<double>(v.x)));
		arr.Push(JsonValue(static_cast<double>(v.y)));
		arr.Push(JsonValue(static_cast<double>(v.z)));
		return arr;
	}

	Vector3 JsonToVec3(const JsonValue& v, const Vector3& fallback = {}) {
		if (!v.IsArray() || v.Size() < 3) return fallback;
		return {
			static_cast<float>(v[0].AsDouble(fallback.x)),
			static_cast<float>(v[1].AsDouble(fallback.y)),
			static_cast<float>(v[2].AsDouble(fallback.z))
		};
	}
}

PrefabManager* PrefabManager::GetInstance() {
	static PrefabManager instance;
	return &instance;
}

const char* PrefabManager::GetPrefabDir() {
	return kPrefabDir;
}

void PrefabManager::Rescan() {
	prefabs_.clear();
	std::filesystem::path dir(kPrefabDir);
	std::error_code ec;
	if (!std::filesystem::exists(dir, ec)) {
		return; // 未作成なら何もしない（プリファブ無し状態）
	}
	for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
		if (ec) break;
		if (!entry.is_regular_file()) continue;
		if (entry.path().extension() != ".json") continue;

		PrefabDef def;
		if (LoadFile(entry.path().generic_string(), def)) {
			prefabs_.push_back(std::move(def));
		}
	}
}

const PrefabDef* PrefabManager::Find(const std::string& name) const {
	for (const auto& p : prefabs_) {
		if (p.name == name) return &p;
	}
	return nullptr;
}

bool PrefabManager::LoadFile(const std::string& filePath, PrefabDef& out) const {
	auto result = JsonParser::ParseFile(filePath);
	if (!result.success) {
		Log(std::string("[PrefabManager] Parse error in ") + filePath + ": " + result.errorMessage + "\n");
		return false;
	}
	const JsonValue& root = result.value;

	// 名前はファイル名から（拡張子無し）
	std::filesystem::path p(filePath);
	out.name = p.stem().string();

	out.modelDir = root["model"]["dir"].AsString();
	out.modelFile = root["model"]["file"].AsString();
	out.tag = TagFromName(root["tag"].AsString());

	out.defaultScale = JsonToVec3(root["defaults"]["scale"], { 1,1,1 });
	out.defaultRotate = JsonToVec3(root["defaults"]["rotate"], { 0,0,0 });

	const JsonValue& col = root["collider"];
	out.hasCollider = col.IsObject() && col.GetType() != JsonValue::Type::Null;
	if (out.hasCollider) {
		out.colliderRadius = static_cast<float>(col["radius"].AsDouble(1.0));
		out.colliderOffset = JsonToVec3(col["offset"], { 0,0,0 });
	}
	return true;
}

bool PrefabManager::Save(const PrefabDef& def, const std::string& filePath) {
	JsonValue root = JsonValue::MakeObject();

	JsonValue modelObj = JsonValue::MakeObject();
	modelObj["dir"] = def.modelDir;
	modelObj["file"] = def.modelFile;
	root["model"] = std::move(modelObj);

	root["tag"] = std::string(GetTagName(def.tag));

	JsonValue defaultsObj = JsonValue::MakeObject();
	defaultsObj["scale"] = Vec3ToJson(def.defaultScale);
	defaultsObj["rotate"] = Vec3ToJson(def.defaultRotate);
	root["defaults"] = std::move(defaultsObj);

	if (def.hasCollider) {
		JsonValue colObj = JsonValue::MakeObject();
		colObj["radius"] = static_cast<double>(def.colliderRadius);
		colObj["offset"] = Vec3ToJson(def.colliderOffset);
		root["collider"] = std::move(colObj);
	}

	// ディレクトリ作成
	std::filesystem::path path(filePath);
	if (path.has_parent_path()) {
		std::error_code ec;
		std::filesystem::create_directories(path.parent_path(), ec);
	}
	return JsonWriter::WriteFile(filePath, root, { true, 2 });
}
