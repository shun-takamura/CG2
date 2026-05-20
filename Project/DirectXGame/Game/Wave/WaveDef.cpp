#include "WaveDef.h"

#include "Json/JsonParser.h"
#include "Json/JsonValue.h"
#include "Json/JsonWriter.h"

namespace {
	Vector3 JsonToVec3(const JsonValue& v) {
		if (!v.IsArray() || v.Size() < 3) return {};
		return {
			static_cast<float>(v[0].AsDouble()),
			static_cast<float>(v[1].AsDouble()),
			static_cast<float>(v[2].AsDouble()),
		};
	}
	JsonValue Vec3ToJson(const Vector3& v) {
		JsonValue arr = JsonValue::MakeArray();
		arr.Push(JsonValue(static_cast<double>(v.x)));
		arr.Push(JsonValue(static_cast<double>(v.y)));
		arr.Push(JsonValue(static_cast<double>(v.z)));
		return arr;
	}
}

namespace WaveDefIO {

	bool LoadFromFile(const std::string& filePath, WaveDef& out) {
		auto result = JsonParser::ParseFile(filePath);
		if (!result.success || !result.value.IsObject()) return false;
		const JsonValue& root = result.value;

		if (root["name"].IsString()) out.name = root["name"].AsString();

		const JsonValue& arr = root["spawn_entries"];
		if (arr.IsArray()) {
			out.entries.clear();
			out.entries.reserve(arr.Size());
			for (size_t i = 0; i < arr.Size(); ++i) {
				const JsonValue& e = arr[i];
				WaveEntry entry{};
				entry.enemyType = e["enemy_type"].AsString();
				entry.prefab    = e["prefab"].AsString();
				entry.triggerT  = static_cast<float>(e["trigger_t"].AsDouble(entry.triggerT));
				entry.retreatT  = static_cast<float>(e["retreat_t"].AsDouble(entry.retreatT));
				entry.speed     = static_cast<float>(e["speed"].AsDouble(entry.speed));
				entry.splineId  = e["spline_id"].AsString();
				entry.count     = static_cast<int>(e["count"].AsDouble(entry.count));

				const JsonValue& pos = e["positions"];
				if (pos.IsArray()) {
					entry.positions.reserve(pos.Size());
					for (size_t j = 0; j < pos.Size(); ++j) {
						entry.positions.push_back(JsonToVec3(pos[j]));
					}
				}
				out.entries.push_back(std::move(entry));
			}
		}
		return true;
	}

	bool SaveToFile(const std::string& filePath, const WaveDef& def) {
		JsonValue root = JsonValue::MakeObject();
		root["name"] = def.name;

		JsonValue arr = JsonValue::MakeArray();
		for (const auto& e : def.entries) {
			JsonValue o = JsonValue::MakeObject();
			o["enemy_type"] = e.enemyType;
			o["prefab"]     = e.prefab;
			o["trigger_t"]  = static_cast<double>(e.triggerT);
			o["retreat_t"]  = static_cast<double>(e.retreatT);
			o["speed"]      = static_cast<double>(e.speed);
			o["spline_id"]  = e.splineId;
			o["count"]      = static_cast<double>(e.count);

			if (!e.positions.empty()) {
				JsonValue posArr = JsonValue::MakeArray();
				for (const auto& p : e.positions) {
					posArr.Push(Vec3ToJson(p));
				}
				o["positions"] = std::move(posArr);
			}
			arr.Push(std::move(o));
		}
		root["spawn_entries"] = std::move(arr);

		return JsonWriter::WriteFile(filePath, root, { true, 2 });
	}

}
