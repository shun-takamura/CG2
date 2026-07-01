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
				// 時間系は秒で読む。旧フォーマット（*_t = カメラ進行度）は 125秒スケールで換算。
				// （旧データはカメラ速度 0.008 t/秒 ＝ 全体尺 125 秒で作成されていた）
				constexpr double kLegacyStageSeconds = 125.0;
				if (e["trigger_sec"].IsNumber())      entry.triggerSec = static_cast<float>(e["trigger_sec"].AsDouble(entry.triggerSec));
				else if (e["trigger_t"].IsNumber())   entry.triggerSec = static_cast<float>(e["trigger_t"].AsDouble(0.0) * kLegacyStageSeconds);
				if (e["retreat_sec"].IsNumber())      entry.retreatSec = static_cast<float>(e["retreat_sec"].AsDouble(entry.retreatSec));
				else if (e["retreat_t"].IsNumber())   { double rt = e["retreat_t"].AsDouble(-1.0); entry.retreatSec = (rt < 0.0) ? -1.0f : static_cast<float>(rt * kLegacyStageSeconds); }
				if (e["traverse_sec"].IsNumber())     entry.traverseSec = static_cast<float>(e["traverse_sec"].AsDouble(entry.traverseSec));
				else if (e["traverse_t"].IsNumber())  entry.traverseSec = static_cast<float>(e["traverse_t"].AsDouble(0.0) * kLegacyStageSeconds);
				entry.splineId  = e["spline_id"].AsString();
				entry.count          = static_cast<int>(e["count"].AsDouble(entry.count));
			if (e["shoot_interval_sec"].IsNumber())    entry.shootIntervalSec = static_cast<float>(e["shoot_interval_sec"].AsDouble(entry.shootIntervalSec));
			else if (e["shoot_interval_t"].IsNumber()) entry.shootIntervalSec = static_cast<float>(e["shoot_interval_t"].AsDouble(0.0) * kLegacyStageSeconds);
			entry.spawnIntervalSec = static_cast<float>(e["spawn_interval_sec"].AsDouble(entry.spawnIntervalSec));
			entry.spawnLimit     = static_cast<int>(e["spawn_limit"].AsDouble(entry.spawnLimit));
			entry.childPrefab    = e["child_prefab"].AsString();
			entry.childSplineId  = e["child_spline_id"].AsString();

				const JsonValue& off = e["camera_offset"];
				if (off.IsArray() && off.Size() >= 3) {
					entry.useCameraOffset = true;
					entry.cameraOffset    = JsonToVec3(off);
				}

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
			o["trigger_sec"]  = static_cast<double>(e.triggerSec);
			o["retreat_sec"]  = static_cast<double>(e.retreatSec);
			o["traverse_sec"] = static_cast<double>(e.traverseSec);
			o["spline_id"]  = e.splineId;
			o["count"]           = static_cast<double>(e.count);
			o["shoot_interval_sec"] = static_cast<double>(e.shootIntervalSec);
			o["spawn_interval_sec"] = static_cast<double>(e.spawnIntervalSec);
			o["spawn_limit"]       = static_cast<double>(e.spawnLimit);
			o["child_prefab"]      = e.childPrefab;
			o["child_spline_id"]   = e.childSplineId;

			if (e.useCameraOffset) {
				o["camera_offset"] = Vec3ToJson(e.cameraOffset);
			}

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
