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
		/// <summary>読み込み元の JSON を受け取る</summary>
		/// <param name="json">読み込み元の JSON オブジェクト（Reader より長く生きること）</param>
		explicit Reader(const JsonValue& json) : json_(json) {}
		/// <summary>キーの値を float として読む（無ければ今の値のまま）</summary>
		/// <param name="key">キー</param>
		/// <param name="v">読み込み先</param>
		void operator()(const char* key, float& v) const { v = static_cast<float>(json_[key].AsDouble(v)); }
		/// <summary>キーの値を int として読む（無ければ今の値のまま）</summary>
		/// <param name="key">キー</param>
		/// <param name="v">読み込み先</param>
		void operator()(const char* key, int& v) const { v = static_cast<int>(json_[key].AsInt(v)); }
		/// <summary>キーの値を bool として読む（無ければ今の値のまま）</summary>
		/// <param name="key">キー</param>
		/// <param name="v">読み込み先</param>
		void operator()(const char* key, bool& v) const { v = json_[key].AsBool(v); }
		/// <summary>キーの値（要素 2 つ以上の配列）を Vector2 として読む（配列でなければ今の値のまま）</summary>
		/// <param name="key">キー</param>
		/// <param name="v">読み込み先</param>
		void operator()(const char* key, Vector2& v) const {
			const JsonValue& a = json_[key];
			if (!a.IsArray() || a.Size() < kVector2ArraySize) return;
			v = { static_cast<float>(a[0].AsDouble(v.x)), static_cast<float>(a[1].AsDouble(v.y)) };
		}
		/// <summary>キーの値（要素 3 つ以上の配列）を Vector3 として読む（配列でなければ今の値のまま）</summary>
		/// <param name="key">キー</param>
		/// <param name="v">読み込み先</param>
		void operator()(const char* key, Vector3& v) const {
			const JsonValue& a = json_[key];
			if (!a.IsArray() || a.Size() < kVector3ArraySize) return;
			v = { static_cast<float>(a[0].AsDouble(v.x)), static_cast<float>(a[1].AsDouble(v.y)), static_cast<float>(a[2].AsDouble(v.z)) };
		}

	private:
		// Vector2 として読む配列に要る要素の数
		static constexpr int kVector2ArraySize = 2;
		// Vector3 として読む配列に要る要素の数
		static constexpr int kVector3ArraySize = 3;

		const JsonValue& json_;
	};

	class Writer {
	public:
		/// <summary>書き込み先の JSON を受け取る</summary>
		/// <param name="json">書き込み先の JSON オブジェクト（Writer より長く生きること）</param>
		explicit Writer(JsonValue& json) : json_(json) {}
		/// <summary>float の値をキーへ書く</summary>
		/// <param name="key">キー</param>
		/// <param name="v">書く値</param>
		void operator()(const char* key, const float& v) const { json_[key] = JsonValue(static_cast<double>(v)); }
		/// <summary>int の値をキーへ書く</summary>
		/// <param name="key">キー</param>
		/// <param name="v">書く値</param>
		void operator()(const char* key, const int& v) const { json_[key] = JsonValue(static_cast<int64_t>(v)); }
		/// <summary>bool の値をキーへ書く</summary>
		/// <param name="key">キー</param>
		/// <param name="v">書く値</param>
		void operator()(const char* key, const bool& v) const { json_[key] = JsonValue(v); }
		/// <summary>Vector2 の値を要素 2 つの配列にしてキーへ書く</summary>
		/// <param name="key">キー</param>
		/// <param name="v">書く値</param>
		void operator()(const char* key, const Vector2& v) const {
			JsonValue a = JsonValue::MakeArray();
			a.Push(JsonValue(static_cast<double>(v.x)));
			a.Push(JsonValue(static_cast<double>(v.y)));
			json_[key] = std::move(a);
		}
		/// <summary>Vector3 の値を要素 3 つの配列にしてキーへ書く</summary>
		/// <param name="key">キー</param>
		/// <param name="v">書く値</param>
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

	/// <summary>Settings（Visit を持つ）を JSON から読む（無いキーは今の値のまま。オブジェクトでなければ何もしない）</summary>
	/// <param name="json">読み込み元の JSON オブジェクト</param>
	/// <param name="settings">読み込み先</param>
	template <class Settings>
	void Load(const JsonValue& json, Settings& settings) {
		if (!json.IsObject()) return;
		Reader reader(json);
		settings.Visit(reader);
	}

	/// <summary>Settings（Visit を持つ）を JSON のオブジェクトにして返す</summary>
	/// <param name="settings">書き出す値</param>
	/// <returns>キーと値を並べた JSON オブジェクト</returns>
	template <class Settings>
	JsonValue Save(const Settings& settings) {
		JsonValue json = JsonValue::MakeObject();
		Writer writer(json);
		Settings copy = settings;   // Visit は書き換え用の参照を渡すので、写しに通す
		copy.Visit(writer);
		return json;
	}

} // namespace TransitionTuning
