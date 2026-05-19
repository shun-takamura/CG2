#include "WaveDef.h"

#include "Json/JsonParser.h"
#include "Json/JsonValue.h"
#include "Json/JsonWriter.h"

namespace WaveDefIO {

	bool LoadFromFile(const std::string& filePath, WaveDef& out) {
		auto result = JsonParser::ParseFile(filePath);
		if (!result.success || !result.value.IsObject()) {
			return false;
		}
		const JsonValue& root = result.value;

		if (root["name"].IsString()) out.name = root["name"].AsString();

		const JsonValue& arr = root["entries"];
		if (arr.IsArray()) {
			out.entries.clear();
			out.entries.reserve(arr.Size());
			for (size_t i = 0; i < arr.Size(); ++i) {
				const JsonValue& e = arr[i];
				WaveEntry entry{};
				entry.time   = static_cast<float>(e["time"].AsDouble(entry.time));
				entry.prefab = e["prefab"].AsString();
				entry.splineName = e["splineName"].AsString();
				entry.speed  = static_cast<float>(e["speed"].AsDouble(entry.speed));
				if (e["removeAtEnd"].IsBool()) {
					entry.removeAtEnd = e["removeAtEnd"].AsBool(entry.removeAtEnd);
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
			o["time"]   = static_cast<double>(e.time);
			o["prefab"] = e.prefab;
			o["splineName"] = e.splineName;
			o["speed"]  = static_cast<double>(e.speed);
			o["removeAtEnd"] = e.removeAtEnd;
			arr.Push(std::move(o));
		}
		root["entries"] = std::move(arr);

		return JsonWriter::WriteFile(filePath, root, { true, 2 });
	}

}
