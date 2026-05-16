#include "JsonValue.h"

#include <cassert>

namespace {
	// 読み取り専用アクセスで存在しないキーを返すときに使う共有のNull値
	const JsonValue kNullSentinel{};
	const std::string kEmptyString{};
	const JsonValue::Array kEmptyArray{};
	const JsonValue::Object kEmptyObject{};
}

JsonValue::JsonValue() : type_(Type::Null), intValue_(0) {}

JsonValue::JsonValue(std::nullptr_t) : type_(Type::Null), intValue_(0) {}

JsonValue::JsonValue(bool value) : type_(Type::Bool), boolValue_(value) {}

JsonValue::JsonValue(int value) : type_(Type::Int), intValue_(static_cast<int64_t>(value)) {}

JsonValue::JsonValue(int64_t value) : type_(Type::Int), intValue_(value) {}

JsonValue::JsonValue(double value) : type_(Type::Double), doubleValue_(value) {}

JsonValue::JsonValue(const char* value)
	: type_(Type::String), intValue_(0), stringValue_(value ? value : "") {}

JsonValue::JsonValue(std::string value)
	: type_(Type::String), intValue_(0), stringValue_(std::move(value)) {}

JsonValue::JsonValue(Array value)
	: type_(Type::Array), intValue_(0),
	arrayValue_(std::make_unique<Array>(std::move(value))) {}

JsonValue::JsonValue(Object value)
	: type_(Type::Object), intValue_(0),
	objectValue_(std::make_unique<Object>(std::move(value))) {}

JsonValue::JsonValue(const JsonValue& other) : type_(Type::Null), intValue_(0) {
	CopyFrom(other);
}

JsonValue::JsonValue(JsonValue&& other) noexcept : type_(Type::Null), intValue_(0) {
	MoveFrom(std::move(other));
}

JsonValue& JsonValue::operator=(const JsonValue& other) {
	if (this != &other) {
		Destroy();
		CopyFrom(other);
	}
	return *this;
}

JsonValue& JsonValue::operator=(JsonValue&& other) noexcept {
	if (this != &other) {
		Destroy();
		MoveFrom(std::move(other));
	}
	return *this;
}

JsonValue::~JsonValue() {
	Destroy();
}

void JsonValue::Destroy() {
	stringValue_.clear();
	arrayValue_.reset();
	objectValue_.reset();
	type_ = Type::Null;
	intValue_ = 0;
}

void JsonValue::CopyFrom(const JsonValue& other) {
	type_ = other.type_;
	switch (other.type_) {
	case Type::Null:
		intValue_ = 0;
		break;
	case Type::Bool:
		boolValue_ = other.boolValue_;
		break;
	case Type::Int:
		intValue_ = other.intValue_;
		break;
	case Type::Double:
		doubleValue_ = other.doubleValue_;
		break;
	case Type::String:
		stringValue_ = other.stringValue_;
		break;
	case Type::Array:
		arrayValue_ = std::make_unique<Array>(*other.arrayValue_);
		break;
	case Type::Object:
		objectValue_ = std::make_unique<Object>(*other.objectValue_);
		break;
	}
}

void JsonValue::MoveFrom(JsonValue&& other) noexcept {
	type_ = other.type_;
	switch (other.type_) {
	case Type::Null:
		intValue_ = 0;
		break;
	case Type::Bool:
		boolValue_ = other.boolValue_;
		break;
	case Type::Int:
		intValue_ = other.intValue_;
		break;
	case Type::Double:
		doubleValue_ = other.doubleValue_;
		break;
	case Type::String:
		stringValue_ = std::move(other.stringValue_);
		break;
	case Type::Array:
		arrayValue_ = std::move(other.arrayValue_);
		break;
	case Type::Object:
		objectValue_ = std::move(other.objectValue_);
		break;
	}
	other.type_ = Type::Null;
	other.intValue_ = 0;
}

bool JsonValue::AsBool(bool defaultValue) const {
	if (type_ == Type::Bool) return boolValue_;
	return defaultValue;
}

int64_t JsonValue::AsInt(int64_t defaultValue) const {
	if (type_ == Type::Int) return intValue_;
	if (type_ == Type::Double) return static_cast<int64_t>(doubleValue_);
	return defaultValue;
}

double JsonValue::AsDouble(double defaultValue) const {
	if (type_ == Type::Double) return doubleValue_;
	if (type_ == Type::Int) return static_cast<double>(intValue_);
	return defaultValue;
}

const std::string& JsonValue::AsString() const {
	if (type_ == Type::String) return stringValue_;
	return kEmptyString;
}

std::string JsonValue::AsString(const std::string& defaultValue) const {
	if (type_ == Type::String) return stringValue_;
	return defaultValue;
}

const JsonValue::Array& JsonValue::AsArray() const {
	if (type_ == Type::Array) return *arrayValue_;
	return kEmptyArray;
}

const JsonValue::Object& JsonValue::AsObject() const {
	if (type_ == Type::Object) return *objectValue_;
	return kEmptyObject;
}

JsonValue::Array& JsonValue::AsArray() {
	assert(type_ == Type::Array);
	return *arrayValue_;
}

JsonValue::Object& JsonValue::AsObject() {
	assert(type_ == Type::Object);
	return *objectValue_;
}

bool JsonValue::Contains(const std::string& key) const {
	return Find(key) != nullptr;
}

const JsonValue* JsonValue::Find(const std::string& key) const {
	if (type_ != Type::Object) return nullptr;
	for (const auto& entry : *objectValue_) {
		if (entry.first == key) return &entry.second;
	}
	return nullptr;
}

JsonValue* JsonValue::Find(const std::string& key) {
	if (type_ != Type::Object) return nullptr;
	for (auto& entry : *objectValue_) {
		if (entry.first == key) return &entry.second;
	}
	return nullptr;
}

JsonValue& JsonValue::operator[](const std::string& key) {
	// Object以外なら自動的にObjectに変換する
	if (type_ != Type::Object) {
		Destroy();
		type_ = Type::Object;
		objectValue_ = std::make_unique<Object>();
	}
	for (auto& entry : *objectValue_) {
		if (entry.first == key) return entry.second;
	}
	objectValue_->emplace_back(key, JsonValue{});
	return objectValue_->back().second;
}

const JsonValue& JsonValue::operator[](const std::string& key) const {
	const JsonValue* found = Find(key);
	return found ? *found : kNullSentinel;
}

size_t JsonValue::Size() const {
	if (type_ == Type::Array) return arrayValue_->size();
	if (type_ == Type::Object) return objectValue_->size();
	if (type_ == Type::String) return stringValue_.size();
	return 0;
}

const JsonValue& JsonValue::operator[](size_t index) const {
	if (type_ == Type::Array && index < arrayValue_->size()) {
		return (*arrayValue_)[index];
	}
	return kNullSentinel;
}

JsonValue& JsonValue::operator[](size_t index) {
	assert(type_ == Type::Array && index < arrayValue_->size());
	return (*arrayValue_)[index];
}

void JsonValue::Push(JsonValue value) {
	if (type_ != Type::Array) {
		Destroy();
		type_ = Type::Array;
		arrayValue_ = std::make_unique<Array>();
	}
	arrayValue_->push_back(std::move(value));
}

const JsonValue& JsonValue::Null() {
	return kNullSentinel;
}
