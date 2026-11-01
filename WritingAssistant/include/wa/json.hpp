// Minimal, dependency-free JSON value type with a parser and serializer.
// Good enough for building Claude API requests and reading responses.
#pragma once

#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

namespace wa {

class JsonError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class Json {
public:
    using Array = std::vector<Json>;
    using Object = std::map<std::string, Json>;

    Json() : value_(nullptr) {}
    Json(std::nullptr_t) : value_(nullptr) {}
    Json(bool b) : value_(b) {}
    Json(int n) : value_(static_cast<double>(n)) {}
    Json(long n) : value_(static_cast<double>(n)) {}
    Json(long long n) : value_(static_cast<double>(n)) {}
    Json(double n) : value_(n) {}
    Json(const char* s) : value_(std::string(s)) {}
    Json(std::string s) : value_(std::move(s)) {}
    Json(Array a) : value_(std::move(a)) {}
    Json(Object o) : value_(std::move(o)) {}

    static Json array() { return Json(Array{}); }
    static Json object() { return Json(Object{}); }

    bool isNull() const { return std::holds_alternative<std::nullptr_t>(value_); }
    bool isBool() const { return std::holds_alternative<bool>(value_); }
    bool isNumber() const { return std::holds_alternative<double>(value_); }
    bool isString() const { return std::holds_alternative<std::string>(value_); }
    bool isArray() const { return std::holds_alternative<Array>(value_); }
    bool isObject() const { return std::holds_alternative<Object>(value_); }

    bool asBool() const;
    double asNumber() const;
    const std::string& asString() const;
    const Array& asArray() const;
    Array& asArray();
    const Object& asObject() const;
    Object& asObject();

    // Object access. operator[] on a null value turns it into an object.
    Json& operator[](const std::string& key);
    const Json* find(const std::string& key) const;
    bool contains(const std::string& key) const { return find(key) != nullptr; }
    // Returns the string at key, or `fallback` if missing / not a string.
    std::string getString(const std::string& key, const std::string& fallback = "") const;

    void push_back(Json v);

    std::string dump() const;
    static Json parse(const std::string& text);

    friend bool operator==(const Json& a, const Json& b) { return a.value_ == b.value_; }
    friend bool operator!=(const Json& a, const Json& b) { return !(a == b); }

private:
    std::variant<std::nullptr_t, bool, double, std::string, Array, Object> value_;
};

std::string jsonEscape(const std::string& s);

}  // namespace wa
