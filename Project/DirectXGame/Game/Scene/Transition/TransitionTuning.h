#pragma once
#include "Json/JsonValue.h"
#include "Vector2.h"
#include "Vector3.h"
#include <cstdint>
#include <utility>

/// <summary>
/// トランジションの調整値を JSON と行き来させる手伝い役。
/// 各 Settings に「キーと変数の一覧」を Visit として 1 か所だけ書き、
/// 同じ一覧を Reader（JSON → 変数）と Writer（変数 → JSON）の両方に通す（読みと書きでキーがずれない）。
/// 読むときに JSON に無いキーは、変数の今の値（既定値）のまま残る。
/// 保存先は Resources/Json/Tuning/Transition.json（TransitionManager が読み書きする）。
/// </summary>
namespace TransitionTuning {

	class Reader {
	public:
		explicit Reader(const JsonValue& json) : json_(json) {}
		void operator()(const char* key, float& v) const { v = static_cast<float>(json_[key].AsDouble(v)); }
		void operator()(const char* key, int& v) const { v = static_cast<int>(json_[key].AsInt(v)); }
		void operator()(const char* key, bool& v) const { v = json_[key].AsBool(v); }
		void operator()(const char* key, Vector2& v) const {
			const JsonValue& a = json_[key];
			if (!a.IsArray() || a.Size() < 2) return;
			v = { static_cast<float>(a[0].AsDouble(v.x)), static_cast<float>(a[1].AsDouble(v.y)) };
		}
		void operator()(const char* key, Vector3& v) const {
			const JsonValue& a = json_[key];
			if (!a.IsArray() || a.Size() < 3) return;
			v = { static_cast<float>(a[0].AsDouble(v.x)), static_cast<float>(a[1].AsDouble(v.y)), static_cast<float>(a[2].AsDouble(v.z)) };
		}

	private:
		const JsonValue& json_;
	};

	class Writer {
	public:
		explicit Writer(JsonValue& json) : json_(json) {}
		void operator()(const char* key, const float& v) const { json_[key] = JsonValue(static_cast<double>(v)); }
		void operator()(const char* key, const int& v) const { json_[key] = JsonValue(static_cast<int64_t>(v)); }
		void operator()(const char* key, const bool& v) const { json_[key] = JsonValue(v); }
		void operator()(const char* key, const Vector2& v) const {
			JsonValue a = JsonValue::MakeArray();
			a.Push(JsonValue(static_cast<double>(v.x)));
			a.Push(JsonValue(static_cast<double>(v.y)));
			json_[key] = std::move(a);
		}
		void operator()(const char* key, const Vector3& v) const {
			JsonValue a = JsonValue::MakeArray();
			a.Push(JsonValue(static_cast<double>(v.x)));
			a.Push(JsonValue(static_cast<double>(v.y)));
			a.Push(JsonValue(static_cast<double>(v.z)));
			json_[key] = std::move(a);
		}

	private:
		JsonValue& json_;
	};

	/// <summary>Settings（Visit を持つ）を JSON から読む（無いキーは今の値のまま）</summary>
	template <class Settings>
	void Load(const JsonValue& json, Settings& settings) {
		if (!json.IsObject()) return;
		Reader reader(json);
		settings.Visit(reader);
	}

	/// <summary>Settings（Visit を持つ）を JSON のオブジェクトにして返す</summary>
	template <class Settings>
	JsonValue Save(const Settings& settings) {
		JsonValue json = JsonValue::MakeObject();
		Writer writer(json);
		Settings copy = settings;   // Visit は書き換え用の参照を渡すので、写しに通す
		copy.Visit(writer);
		return json;
	}

} // namespace TransitionTuning
