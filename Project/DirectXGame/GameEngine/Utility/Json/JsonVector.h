#pragma once
#include "Json/JsonValue.h"
#include "Vector2.h"
#include "Vector3.h"
#include "Vector4.h"

#include <cstddef>

// Vector2 / Vector3 / Vector4 と JSON の数値の配列（[x, y] / [x, y, z] / [x, y, z, w]）を相互に変換する。
// 読み込みは「配列でない・要素が足りない時は fallback をそのまま返す」「要素が数値でない時はその成分だけ fallback の値」にそろえる。

namespace JsonVectorDetail {
	// Vector2 の成分の数
	inline constexpr size_t kVector2ComponentCount = 2;
	// Vector3 の成分の数
	inline constexpr size_t kVector3ComponentCount = 3;
	// Vector4 の成分の数
	inline constexpr size_t kVector4ComponentCount = 4;
}

/// <summary>Vector2 を [x, y] の JSON 配列にする</summary>
/// <param name="v">書き出す値</param>
/// <returns>要素 2 つの数値の配列</returns>
inline JsonValue MakeVector2(const Vector2& v) {
	JsonValue array = JsonValue::MakeArray();
	array.Push(JsonValue(static_cast<double>(v.x)));
	array.Push(JsonValue(static_cast<double>(v.y)));
	return array;
}

/// <summary>Vector3 を [x, y, z] の JSON 配列にする</summary>
/// <param name="v">書き出す値</param>
/// <returns>要素 3 つの数値の配列</returns>
inline JsonValue MakeVector3(const Vector3& v) {
	JsonValue array = JsonValue::MakeArray();
	array.Push(JsonValue(static_cast<double>(v.x)));
	array.Push(JsonValue(static_cast<double>(v.y)));
	array.Push(JsonValue(static_cast<double>(v.z)));
	return array;
}

/// <summary>Vector4 を [x, y, z, w] の JSON 配列にする</summary>
/// <param name="v">書き出す値</param>
/// <returns>要素 4 つの数値の配列</returns>
inline JsonValue MakeVector4(const Vector4& v) {
	JsonValue array = JsonValue::MakeArray();
	array.Push(JsonValue(static_cast<double>(v.x)));
	array.Push(JsonValue(static_cast<double>(v.y)));
	array.Push(JsonValue(static_cast<double>(v.z)));
	array.Push(JsonValue(static_cast<double>(v.w)));
	return array;
}

/// <summary>[x, y] の JSON 配列を Vector2 として読む</summary>
/// <param name="json">読み込み元（数値の配列）</param>
/// <param name="fallback">配列でない・要素が足りない時に返す値。数値でない要素はこの値の同じ成分になる</param>
/// <returns>読んだ値</returns>
inline Vector2 ReadVector2(const JsonValue& json, const Vector2& fallback = {}) {
	if (!json.IsArray() || json.Size() < JsonVectorDetail::kVector2ComponentCount) {
		return fallback;
	}
	return {
		static_cast<float>(json[0].AsDouble(fallback.x)),
		static_cast<float>(json[1].AsDouble(fallback.y)),
	};
}

/// <summary>[x, y, z] の JSON 配列を Vector3 として読む</summary>
/// <param name="json">読み込み元（数値の配列）</param>
/// <param name="fallback">配列でない・要素が足りない時に返す値。数値でない要素はこの値の同じ成分になる</param>
/// <returns>読んだ値</returns>
inline Vector3 ReadVector3(const JsonValue& json, const Vector3& fallback = {}) {
	if (!json.IsArray() || json.Size() < JsonVectorDetail::kVector3ComponentCount) {
		return fallback;
	}
	return {
		static_cast<float>(json[0].AsDouble(fallback.x)),
		static_cast<float>(json[1].AsDouble(fallback.y)),
		static_cast<float>(json[2].AsDouble(fallback.z)),
	};
}

/// <summary>[x, y, z, w] の JSON 配列を Vector4 として読む</summary>
/// <param name="json">読み込み元（数値の配列）</param>
/// <param name="fallback">配列でない・要素が足りない時に返す値。数値でない要素はこの値の同じ成分になる</param>
/// <returns>読んだ値</returns>
inline Vector4 ReadVector4(const JsonValue& json, const Vector4& fallback = {}) {
	if (!json.IsArray() || json.Size() < JsonVectorDetail::kVector4ComponentCount) {
		return fallback;
	}
	return {
		static_cast<float>(json[0].AsDouble(fallback.x)),
		static_cast<float>(json[1].AsDouble(fallback.y)),
		static_cast<float>(json[2].AsDouble(fallback.z)),
		static_cast<float>(json[3].AsDouble(fallback.w)),
	};
}
