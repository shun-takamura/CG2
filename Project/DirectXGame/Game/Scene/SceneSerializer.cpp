#include "SceneSerializer.h"

#include <filesystem>

#include "Json/JsonParser.h"
#include "Json/JsonValue.h"
#include "Json/JsonVector.h"
#include "Json/JsonWriter.h"

namespace {
	// シーンの JSON を書き出すときの字下げ [スペースの数]
	constexpr int kJsonIndentWidth = 2;

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

	/// <summary>種別を JSON の "type" 文字列にする</summary>
	/// <param name="kind">種別</param>
	/// <returns>"type" 文字列（表に無ければ "Object3D"）</returns>
	const char* ToTypeName(SceneEntityDesc::Kind kind) {
		for (const auto& kn : kKindNames) {
			if (kn.kind == kind) return kn.name;
		}
		return "Object3D";
	}

	/// <summary>JSON の "type" 文字列を種別にする</summary>
	/// <param name="name">"type" 文字列</param>
	/// <param name="out">種別の書き込み先</param>
	/// <returns>知っている種別なら true（未知なら false で、呼び出し側が読み飛ばす）</returns>
	bool FromTypeName(const std::string& name, SceneEntityDesc::Kind& out) {
		for (const auto& kn : kKindNames) {
			if (name == kn.name) {
				out = kn.kind;
				return true;
			}
		}
		return false;  // 未知の種別は呼び出し側で読み飛ばす
	}

	/// <summary>エントリ 1 つを JSON オブジェクトにする（種別ごとに要るキーだけ書く）</summary>
	/// <param name="d">エントリ</param>
	/// <returns>"objects" 配列の 1 要素</returns>
	JsonValue DescToJson(const SceneEntityDesc& d) {
		JsonValue e = JsonValue::MakeObject();
		e["type"] = std::string(ToTypeName(d.kind));
		e["name"] = d.name;

		// Prefab はタグ・モデルパスをプレハブ定義側が持つので書かない
		if (d.kind != SceneEntityDesc::Kind::Prefab) {
			e["tag"] = std::string(GetTagName(d.tag));
		}
		if (!d.streamGroup.empty()) e["streamGroup"] = d.streamGroup;

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
			for (const auto& p : d.points) pts.Push(MakeVector3(p));
			e["points"] = std::move(pts);
			break;
		}
		}

		// Sprite は 2D 座標、Spline は制御点が位置を持つのでトランスフォームは書かない
		if (d.kind != SceneEntityDesc::Kind::Sprite && d.kind != SceneEntityDesc::Kind::Spline) {
			JsonValue tf = JsonValue::MakeObject();
			tf["scale"] = MakeVector3(d.scale);
			tf["rotate"] = MakeVector3(d.rotate);
			tf["translate"] = MakeVector3(d.translate);
			e["transform"] = std::move(tf);
		}
		return e;
	}

	/// <summary>"objects" 配列の 1 要素をエントリにする</summary>
	/// <param name="e">JSON オブジェクト</param>
	/// <param name="d">エントリの書き込み先</param>
	/// <returns>読めたら true（"type" が未知なら false）</returns>
	bool JsonToDesc(const JsonValue& e, SceneEntityDesc& d) {
		if (!FromTypeName(e["type"].AsString(), d.kind)) return false;

		d.name = e["name"].AsString();
		d.tag = TagFromName(e["tag"].AsString());
		d.streamGroup = e["streamGroup"].AsString();

		const JsonValue& tf = e["transform"];
		d.scale = ReadVector3(tf["scale"], { 1.0f, 1.0f, 1.0f });
		d.rotate = ReadVector3(tf["rotate"], {});
		d.translate = ReadVector3(tf["translate"], {});

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
					d.points.push_back(ReadVector3(pts[i], {}));
				}
			}
			break;
		}
		}
		return true;
	}

}  // namespace

namespace {
	/// <summary>シーン 1 枚分の JSON のルート（"scene" と "objects"）を組む</summary>
	/// <param name="data">シーンの記述</param>
	/// <returns>ルートの JSON オブジェクト</returns>
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
		return JsonWriter::WriteFile(filePath, BuildRoot(data), { true, kJsonIndentWidth });
	}

	std::string ToString(const SceneData& data) {
		return JsonWriter::Write(BuildRoot(data), { true, kJsonIndentWidth });
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
