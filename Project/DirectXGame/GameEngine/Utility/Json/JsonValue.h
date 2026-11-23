#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

/// <summary>
/// JSON値クラス
/// 7 つの型（Null/Bool/Int/Double/String/Array/Object）を保持できる動的型
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
	/// <summary>Null の値を作る</summary>
	JsonValue();
	/// <summary>Null の値を作る（nullptr から）</summary>
	JsonValue(std::nullptr_t);
	/// <summary>Bool の値を作る</summary>
	/// <param name="value">値</param>
	JsonValue(bool value);
	/// <summary>Int の値を作る</summary>
	/// <param name="value">値</param>
	JsonValue(int value);
	/// <summary>Int（int64）の値を作る</summary>
	/// <param name="value">値</param>
	JsonValue(int64_t value);
	/// <summary>Double の値を作る</summary>
	/// <param name="value">値</param>
	JsonValue(double value);
	/// <summary>String の値を作る（nullptr なら空文字）</summary>
	/// <param name="value">文字列（UTF-8）</param>
	JsonValue(const char* value);
	/// <summary>String の値を作る</summary>
	/// <param name="value">文字列（UTF-8）</param>
	JsonValue(std::string value);
	/// <summary>Array の値を作る</summary>
	/// <param name="value">要素</param>
	JsonValue(Array value);
	/// <summary>Object の値を作る</summary>
	/// <param name="value">キーと値の組（挿入順）</param>
	JsonValue(Object value);

	/// <summary>深いコピーで作る（配列・オブジェクトの中身も複製する）</summary>
	/// <param name="other">コピー元</param>
	JsonValue(const JsonValue& other);
	/// <summary>中身を移して作る（移した後の other は Null）</summary>
	/// <param name="other">移し元</param>
	JsonValue(JsonValue&& other) noexcept;
	/// <summary>今の中身を捨てて深いコピーをする</summary>
	/// <param name="other">コピー元</param>
	/// <returns>自分</returns>
	JsonValue& operator=(const JsonValue& other);
	/// <summary>今の中身を捨てて中身を移す（移した後の other は Null）</summary>
	/// <param name="other">移し元</param>
	/// <returns>自分</returns>
	JsonValue& operator=(JsonValue&& other) noexcept;

	~JsonValue();

	//====================
	// 型判定
	//====================
	/// <summary>値の型</summary>
	/// <returns>型</returns>
	Type GetType() const { return type_; }
	/// <summary>Null か</summary>
	/// <returns>Null なら true</returns>
	bool IsNull() const { return type_ == Type::Null; }
	/// <summary>Bool か</summary>
	/// <returns>Bool なら true</returns>
	bool IsBool() const { return type_ == Type::Bool; }
	/// <summary>Int か</summary>
	/// <returns>Int なら true</returns>
	bool IsInt() const { return type_ == Type::Int; }
	/// <summary>Double か</summary>
	/// <returns>Double なら true</returns>
	bool IsDouble() const { return type_ == Type::Double; }
	/// <summary>数値（Int か Double）か</summary>
	/// <returns>数値なら true</returns>
	bool IsNumber() const { return type_ == Type::Int || type_ == Type::Double; }
	/// <summary>String か</summary>
	/// <returns>String なら true</returns>
	bool IsString() const { return type_ == Type::String; }
	/// <summary>Array か</summary>
	/// <returns>Array なら true</returns>
	bool IsArray() const { return type_ == Type::Array; }
	/// <summary>Object か</summary>
	/// <returns>Object なら true</returns>
	bool IsObject() const { return type_ == Type::Object; }

	//====================
	// 値の取得（要求した型と異なる場合はデフォルト値を返す）
	//====================
	/// <summary>Bool として取り出す</summary>
	/// <param name="defaultValue">Bool でない時に返す値</param>
	/// <returns>値（Bool でなければ defaultValue）</returns>
	bool AsBool(bool defaultValue = false) const;
	/// <summary>整数として取り出す（Double は 0 方向へ切り捨てる）</summary>
	/// <param name="defaultValue">数値でない時に返す値</param>
	/// <returns>値（数値でなければ defaultValue）</returns>
	int64_t AsInt(int64_t defaultValue = 0) const;
	/// <summary>Double として取り出す（Int も変換して返す）</summary>
	/// <param name="defaultValue">数値でない時に返す値</param>
	/// <returns>値（数値でなければ defaultValue）</returns>
	double AsDouble(double defaultValue = 0.0) const;
	/// <summary>文字列として参照する</summary>
	/// <returns>文字列（String でなければ共有の空文字）</returns>
	const std::string& AsString() const;
	/// <summary>文字列としてコピーで取り出す</summary>
	/// <param name="defaultValue">String でない時に返す値</param>
	/// <returns>文字列（String でなければ defaultValue）</returns>
	std::string AsString(const std::string& defaultValue) const;
	/// <summary>配列として参照する（読み取り専用）</summary>
	/// <returns>要素（Array でなければ共有の空配列）</returns>
	const Array& AsArray() const;
	/// <summary>オブジェクトとして参照する（読み取り専用）</summary>
	/// <returns>キーと値の組（Object でなければ共有の空オブジェクト）</returns>
	const Object& AsObject() const;

	/// <summary>配列として書き換え用に参照する（Array でなければ assert。Release では未定義動作）</summary>
	/// <returns>要素</returns>
	Array& AsArray();
	/// <summary>オブジェクトとして書き換え用に参照する（Object でなければ assert。Release では未定義動作）</summary>
	/// <returns>キーと値の組</returns>
	Object& AsObject();

	//====================
	// オブジェクト操作
	//====================
	/// <summary>キーがあるか（Object でなければ false）</summary>
	/// <param name="key">キー</param>
	/// <returns>あれば true</returns>
	bool Contains(const std::string& key) const;
	/// <summary>キーで値を探す（先頭から線形に探し、重複キーは最初の物）</summary>
	/// <param name="key">キー</param>
	/// <returns>見つかった値（無い・Object でなければ nullptr）</returns>
	const JsonValue* Find(const std::string& key) const;
	/// <summary>キーで値を探す（書き換え用。先頭から線形に探し、重複キーは最初の物）</summary>
	/// <param name="key">キー</param>
	/// <returns>見つかった値（無い・Object でなければ nullptr）</returns>
	JsonValue* Find(const std::string& key);

	/// <summary>
	/// キー参照（書き込み可能）。存在しない場合は末尾に新規挿入した Null 値を返す。
	/// Object 以外に使うと中身を捨てて空の Object に変える
	/// </summary>
	/// <param name="key">キー</param>
	/// <returns>キーの値</returns>
	JsonValue& operator[](const std::string& key);

	/// <summary>
	/// キー参照（読み取り専用）。存在しない場合はstaticなNullを返す
	/// </summary>
	/// <param name="key">キー</param>
	/// <returns>キーの値（無い・Object でなければ共有の Null）</returns>
	const JsonValue& operator[](const std::string& key) const;

	//====================
	// 配列操作
	//====================
	/// <summary>要素数（Array・Object は要素数、String はバイト数、それ以外は 0）</summary>
	/// <returns>要素数</returns>
	size_t Size() const;
	/// <summary>添字で要素を参照する（読み取り専用）</summary>
	/// <param name="index">添字</param>
	/// <returns>要素（範囲外・Array でなければ共有の Null）</returns>
	const JsonValue& operator[](size_t index) const;
	/// <summary>添字で要素を参照する（書き換え用。範囲外・Array でなければ assert。Release では未定義動作）</summary>
	/// <param name="index">添字</param>
	/// <returns>要素</returns>
	JsonValue& operator[](size_t index);
	/// <summary>末尾に要素を足す。Array 以外に使うと中身を捨てて空の Array に変えてから足す</summary>
	/// <param name="value">足す値</param>
	void Push(JsonValue value);

	//====================
	// 静的ヘルパー
	//====================
	/// <summary>空のオブジェクトを作る</summary>
	/// <returns>Object 型の値</returns>
	static JsonValue MakeObject() { return JsonValue(Object{}); }
	/// <summary>空の配列を作る</summary>
	/// <returns>Array 型の値</returns>
	static JsonValue MakeArray() { return JsonValue(Array{}); }
	/// <summary>共有の Null 値（見つからなかった時の戻り値と同じ物）</summary>
	/// <returns>Null 値への参照</returns>
	static const JsonValue& Null();

private:
	/// <summary>中身を解放して Null にする</summary>
	void Destroy();
	/// <summary>other の型と中身を深いコピーで写す（自分は Destroy 済みの前提）</summary>
	/// <param name="other">コピー元</param>
	void CopyFrom(const JsonValue& other);
	/// <summary>other の型と中身を移し、other を Null にする（自分は Destroy 済みの前提）</summary>
	/// <param name="other">移し元</param>
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
