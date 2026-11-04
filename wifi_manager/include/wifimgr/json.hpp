// Small JSON value used by the control protocol (daemon <-> wifictl).
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace wifimgr {

class Json {
 public:
  enum class Type { Null, Bool, Number, String, Array, Object };
  using Array = std::vector<Json>;
  using Object = std::vector<std::pair<std::string, Json>>;

  Json() = default;
  Json(std::nullptr_t) {}
  Json(bool b) : type_(Type::Bool), bool_(b) {}
  Json(int n) : type_(Type::Number), num_(n) {}
  Json(long n) : type_(Type::Number), num_(static_cast<double>(n)) {}
  Json(long long n) : type_(Type::Number), num_(static_cast<double>(n)) {}
  Json(unsigned n) : type_(Type::Number), num_(n) {}
  Json(double n) : type_(Type::Number), num_(n) {}
  Json(const char* s) : type_(Type::String), str_(s) {}
  Json(std::string s) : type_(Type::String), str_(std::move(s)) {}
  Json(std::string_view s) : type_(Type::String), str_(s) {}

  static Json array() {
    Json j;
    j.type_ = Type::Array;
    return j;
  }
  static Json object() {
    Json j;
    j.type_ = Type::Object;
    return j;
  }
  static std::optional<Json> parse(std::string_view text);

  Type type() const { return type_; }
  bool isNull() const { return type_ == Type::Null; }
  bool isObject() const { return type_ == Type::Object; }
  bool isArray() const { return type_ == Type::Array; }
  bool isString() const { return type_ == Type::String; }
  bool isNumber() const { return type_ == Type::Number; }
  bool isBool() const { return type_ == Type::Bool; }

  bool asBool(bool def = false) const { return type_ == Type::Bool ? bool_ : def; }
  double asNumber(double def = 0) const { return type_ == Type::Number ? num_ : def; }
  long asInt(long def = 0) const {
    return type_ == Type::Number ? static_cast<long>(num_) : def;
  }
  const std::string& asString() const;
  const Array& items() const;
  const Object& members() const;

  // Object: insert or replace a member. Converts Null to Object.
  Json& set(const std::string& key, Json value);
  // Array: append. Converts Null to Array.
  Json& push(Json value);
  // Object lookup; nullptr if absent or not an object.
  const Json* get(std::string_view key) const;
  std::string getString(std::string_view key, const std::string& def = {}) const;
  long getInt(std::string_view key, long def = 0) const;
  bool getBool(std::string_view key, bool def = false) const;

  // Compact single-line serialisation (control protocol is line framed).
  std::string dump() const;

 private:
  void dumpTo(std::string& out) const;

  Type type_ = Type::Null;
  bool bool_ = false;
  double num_ = 0;
  std::string str_;
  Array arr_;
  Object obj_;
};

}  // namespace wifimgr
