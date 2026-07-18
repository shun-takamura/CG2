#include "SceneSerializer.h"

#include <filesystem>

#include "Json/JsonParser.h"
#include "Json/JsonValue.h"
#include "Json/JsonWriter.h"

namespace {

	// ---- Kind <-> JSON の "type" 文字列 ----
	// ここが JSON 形式の唯一の定義箇所。エントリ種別を増やすときはこの表だけ直す。
	struct KindName {
		SceneEntityDesc::Kind kind;
		const char* name;
	};
	constexpr KindName kKindNames[] = {
		{ SceneEntityDesc::Kind::Object3D,         "Object3D"         },
		{ SceneEntityDesc::Kind::AnimatedObject3D, "AnimatedObject3D" },
		{ SceneEntityDesc::Kind::Primitive,        "Primitive"        },
		{ SceneEntityDesc::Kind::Sprite,           "Sprite"           },
		{ SceneEntityDesc::Kind::Spline,           "Spline"           },
		{ SceneEntityDesc::Kind::Prefab,           "Prefab"           },
	};

	const char* ToTypeName(SceneEntityDesc::Kind kind) {
		for (const auto& kn : kKindNames) {
			if (kn.kind == kind) return kn.name;
		}
		return "Object3D";
	}

	bool FromTypeName(const std::string& name, SceneEntityDesc::Kind& out) {
		for (const auto& kn : kKindNames) {
			if (name == kn.name) {
				out = kn.kind;
				return true;
			}
		}
		return false;  // 未知の種別は呼び出し側で読み飛ばす
	}

	JsonValue Vec3ToJson(const Vector3& v) {
		JsonValue arr = JsonValue::MakeArray();
		arr.Push(JsonValue(static_cast<double>(v.x)));
		arr.Push(JsonValue(static_cast<double>(v.y)));
		arr.Push(JsonValue(static_cast<double>(v.z)));
		return arr;
	}

	Vector3 JsonToVec3(const JsonValue& v, const Vector3& fallback) {
		if (!v.IsArray() || v.Size() < 3) return fallback;
		return {
			static_cast<float>(v[0].AsDouble(fallback.x)),
			static_cast<float>(v[1].AsDouble(fallback.y)),
			static_cast<float>(v[2].AsDouble(fallback.z)),
		};
	}

	JsonValue DescToJson(const SceneEntityDesc& d) {
		JsonValue e = JsonValue::MakeObject();
		e["type"] = std::string(ToTypeName(d.kind));
		e["name"] = d.name;

		// Prefab はタグ・モデルパスをプレハブ定義側が持つので書かない
		if (d.kind != SceneEntityDesc::Kind::Prefab) {
			e["tag"] = std::string(GetTagName(d.tag));
		}

		switch (d.kind) {
		case SceneEntityDesc::Kind::Object3D:
		case SceneEntityDesc::Kind::AnimatedObject3D:
			e["dir"] = d.dir;
			e["file"] = d.file;
			break;
		case SceneEntityDesc::Kind::Prefab:
			e["prefab"] = d.prefabName;
			break;
		case SceneEntityDesc::Kind::Primitive:
			e["primitiveType"] = static_cast<int64_t>(d.primitiveType);
			if (!d.texture.empty()) e["texture"] = d.texture;
			break;
		case SceneEntityDesc::Kind::Sprite: {
			e["texture"] = d.texture;
			JsonValue pos = JsonValue::MakeArray();
			pos.Push(JsonValue(static_cast<double>(d.spritePos.x)));
			pos.Push(JsonValue(static_cast<double>(d.spritePos.y)));
			e["pos"] = std::move(pos);
			break;
		}
		case SceneEntityDesc::Kind::Spline: {
			JsonValue pts = JsonValue::MakeArray();
			for (const auto& p : d.points) pts.Push(Vec3ToJson(p));
			e["points"] = std::move(pts);
			break;
		}
		}

		// Sprite は 2D 座標、Spline は制御点が位置を持つのでトランスフォームは書かない
		if (d.kind != SceneEntityDesc::Kind::Sprite && d.kind != SceneEntityDesc::Kind::Spline) {
			JsonValue tf = JsonValue::MakeObject();
			tf["scale"] = Vec3ToJson(d.scale);
			tf["rotate"] = Vec3ToJson(d.rotate);
			tf["translate"] = Vec3ToJson(d.translate);
			e["transform"] = std::move(tf);
		}
		return e;
	}

	bool JsonToDesc(const JsonValue& e, SceneEntityDesc& d) {
		if (!FromTypeName(e["type"].AsString(), d.kind)) return false;

		d.name = e["name"].AsString();
		d.tag = TagFromName(e["tag"].AsString());

		const JsonValue& tf = e["transform"];
		d.scale = JsonToVec3(tf["scale"], { 1.0f, 1.0f, 1.0f });
		d.rotate = JsonToVec3(tf["rotate"], {});
		d.translate = JsonToVec3(tf["translate"], {});

		switch (d.kind) {
		case SceneEntityDesc::Kind::Object3D:
		case SceneEntityDesc::Kind::AnimatedObject3D:
			d.dir = e["dir"].AsString();
			d.file = e["file"].AsString();
			break;
		case SceneEntityDesc::Kind::Prefab:
			d.prefabName = e["prefab"].AsString();
			break;
		case SceneEntityDesc::Kind::Primitive:
			d.primitiveType = static_cast<int>(e["primitiveType"].AsInt());
			d.texture = e["texture"].AsString();
			break;
		case SceneEntityDesc::Kind::Sprite:
			d.texture = e["texture"].AsString();
			d.spritePos = {
				static_cast<float>(e["pos"][0].AsDouble(0.0)),
				static_cast<float>(e["pos"][1].AsDouble(0.0)),
			};
			break;
		case SceneEntityDesc::Kind::Spline: {
			const JsonValue& pts = e["points"];
			if (pts.IsArray()) {
				d.points.reserve(pts.Size());
				for (size_t i = 0; i < pts.Size(); ++i) {
					d.points.push_back(JsonToVec3(pts[i], {}));
				}
			}
			break;
		}
		}
		return true;
	}

}  // namespace

namespace {
	JsonValue BuildRoot(const SceneData& data) {
		JsonValue root = JsonValue::MakeObject();
		root["scene"] = data.sceneName;
		JsonValue arr = JsonValue::MakeArray();
		for (const auto& d : data.entities) {
			arr.Push(DescToJson(d));
		}
		root["objects"] = std::move(arr);
		return root;
	}
}

namespace SceneSerializer {

	bool WriteFile(const std::string& filePath, const SceneData& data) {
		std::filesystem::path path(filePath);
		if (path.has_parent_path()) {
			std::error_code ec;
			std::filesystem::create_directories(path.parent_path(), ec);
		}
		return JsonWriter::WriteFile(filePath, BuildRoot(data), { true, 2 });
	}

	std::string ToString(const SceneData& data) {
		return JsonWriter::Write(BuildRoot(data), { true, 2 });
	}

	bool ReadFile(const std::string& filePath, SceneData& out, std::string* errorMessage) {
		auto result = JsonParser::ParseFile(filePath);
		if (!result.success) {
			if (errorMessage) *errorMessage = result.errorMessage;
			return false;
		}

		// 読み切ってから out へ移す。途中で失敗しても呼び出し側の状態を壊さないため。
		SceneData parsed;
		parsed.sceneName = result.value["scene"].AsString();

		const JsonValue& objs = result.value["objects"];
		if (objs.IsArray()) {
			parsed.entities.reserve(objs.Size());
			for (size_t i = 0; i < objs.Size(); ++i) {
				SceneEntityDesc d;
				if (JsonToDesc(objs[i], d)) {
					parsed.entities.push_back(std::move(d));
				}
			}
		}

		out = std::move(parsed);
		return true;
	}

}  // namespace SceneSerializer
