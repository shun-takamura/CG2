#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

/// <summary>
/// JSON値クラス
/// 6つの型（Null/Bool/Int/Double/String/Array/Object）を保持できる動的型
/// オブジェクトは挿入順を保持する（vector<pair>ベース）
/// </summary>
class JsonValue {
public:
	enum class Type {
		Null,
		Bool,
		Int,
		Double,
		String,
		Array,
		Object,
	};

	using Array = std::vector<JsonValue>;
	using ObjectEntry = std::pair<std::string, JsonValue>;
	using Object = std::vector<ObjectEntry>;

public:
	// コンストラクタ群
	JsonValue();
	JsonValue(std::nullptr_t);
	JsonValue(bool value);
	JsonValue(int value);
	JsonValue(int64_t value);
	JsonValue(double value);
	JsonValue(const char* value);
	JsonValue(std::string value);
	JsonValue(Array value);
	JsonValue(Object value);

	// コピー/ムーブ
	JsonValue(const JsonValue& other);
	JsonValue(JsonValue&& other) noexcept;
	JsonValue& operator=(const JsonValue& other);
	JsonValue& operator=(JsonValue&& other) noexcept;

	~JsonValue();

	//====================
	// 型判定
	//====================
	Type GetType() const { return type_; }
	bool IsNull() const { return type_ == Type::Null; }
	bool IsBool() const { return type_ == Type::Bool; }
	bool IsInt() const { return type_ == Type::Int; }
	bool IsDouble() const { return type_ == Type::Double; }
	bool IsNumber() const { return type_ == Type::Int || type_ == Type::Double; }
	bool IsString() const { return type_ == Type::String; }
	bool IsArray() const { return type_ == Type::Array; }
	bool IsObject() const { return type_ == Type::Object; }

	//====================
	// 値の取得（要求した型と異なる場合はデフォルト値を返す）
	//====================
	bool AsBool(bool defaultValue = false) const;
	int64_t AsInt(int64_t defaultValue = 0) const;
	double AsDouble(double defaultValue = 0.0) const;
	const std::string& AsString() const;
	std::string AsString(const std::string& defaultValue) const;
	const Array& AsArray() const;
	const Object& AsObject() const;

	// 書き込み用アクセサ
	Array& AsArray();
	Object& AsObject();

	//====================
	// オブジェクト操作（型がObject以外なら未定義動作）
	//====================
	bool Contains(const std::string& key) const;
	const JsonValue* Find(const std::string& key) const;
	JsonValue* Find(const std::string& key);

	/// <summary>
	/// キー参照。存在しない場合は新規挿入してNull値を返す（書き込み可能）
	/// </summary>
	JsonValue& operator[](const std::string& key);

	/// <summary>
	/// キー参照（読み取り専用）。存在しない場合はstaticなNullを返す
	/// </summary>
	const JsonValue& operator[](const std::string& key) const;

	//====================
	// 配列操作（型がArray以外なら未定義動作）
	//====================
	size_t Size() const;
	const JsonValue& operator[](size_t index) const;
	JsonValue& operator[](size_t index);
	void Push(JsonValue value);

	//====================
	// 静的ヘルパー
	//====================
	static JsonValue MakeObject() { return JsonValue(Object{}); }
	static JsonValue MakeArray() { return JsonValue(Array{}); }
	static const JsonValue& Null();

private:
	void Destroy();
	void CopyFrom(const JsonValue& other);
	void MoveFrom(JsonValue&& other) noexcept;

private:
	Type type_ = Type::Null;

	// 各型の値（typeに応じてどれか1つが有効）
	union {
		bool boolValue_;
		int64_t intValue_;
		double doubleValue_;
	};
	std::string stringValue_;
	std::unique_ptr<Array> arrayValue_;
	std::unique_ptr<Object> objectValue_;
};
