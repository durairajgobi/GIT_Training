// json.hpp - A small, dependency-free JSON library for C++17.
//
// Supports:
//   * Parsing JSON text into a Json value (strict RFC 8259 grammar)
//   * Serializing (compact or pretty-printed)
//   * Creating / reading / updating / deleting values in objects and arrays
//   * JSON Pointer (RFC 6901) lookups, e.g. "/employees/0/name"
//   * Deep merge of objects
//   * Loading from and saving to files
//
// Objects keep keys in insertion order.

#ifndef SIMPLE_JSON_HPP
#define SIMPLE_JSON_HPP

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <stdexcept>
#include <type_traits>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace sj {

class Json;

using JsonArray  = std::vector<Json>;
using JsonMember = std::pair<std::string, Json>;

// Insertion-ordered object.
class JsonObject {
public:
    using iterator       = std::vector<JsonMember>::iterator;
    using const_iterator = std::vector<JsonMember>::const_iterator;

    Json&       operator[](const std::string& key);       // insert if missing
    Json&       at(const std::string& key);               // throws if missing
    const Json& at(const std::string& key) const;
    bool        contains(const std::string& key) const;
    bool        erase(const std::string& key);
    std::size_t size() const { return members_.size(); }
    bool        empty() const { return members_.empty(); }

    iterator       begin() { return members_.begin(); }
    iterator       end() { return members_.end(); }
    const_iterator begin() const { return members_.begin(); }
    const_iterator end() const { return members_.end(); }

    bool operator==(const JsonObject& other) const;
    bool operator!=(const JsonObject& other) const { return !(*this == other); }

private:
    iterator       find(const std::string& key);
    const_iterator find(const std::string& key) const;
    std::vector<JsonMember> members_;
};

// Thrown for parse errors, type errors and missing keys / indices.
class JsonError : public std::runtime_error {
public:
    explicit JsonError(const std::string& msg) : std::runtime_error(msg) {}
};

enum class JsonType { Null, Boolean, Number, String, Array, Object };

const char* typeName(JsonType t);

class Json {
public:
    // ---- Construction -------------------------------------------------
    Json() : value_(nullptr) {}
    Json(std::nullptr_t) : value_(nullptr) {}
    Json(bool b) : value_(b) {}
    Json(int n) : value_(static_cast<double>(n)) {}
    Json(long n) : value_(static_cast<double>(n)) {}
    Json(long long n) : value_(static_cast<double>(n)) {}
    Json(unsigned n) : value_(static_cast<double>(n)) {}
    Json(unsigned long n) : value_(static_cast<double>(n)) {}
    Json(unsigned long long n) : value_(static_cast<double>(n)) {}
    Json(double d) : value_(d) {}
    Json(const char* s) : value_(std::string(s)) {}
    Json(std::string s) : value_(std::move(s)) {}
    Json(JsonArray a) : value_(std::move(a)) {}
    Json(JsonObject o) : value_(std::move(o)) {}

    // Factory helpers:  Json::array({1, 2, 3})
    //                   Json::object({{"name", "Bob"}, {"age", 30}})
    static Json array(std::initializer_list<Json> items = {});
    static Json object(std::initializer_list<JsonMember> members = {});

    // ---- Parsing & serialization -------------------------------------
    static Json parse(const std::string& text);
    static Json loadFile(const std::string& path);
    void        saveFile(const std::string& path, int indent = 2) const;

    // indent < 0 => compact single line; indent >= 0 => pretty printed.
    std::string dump(int indent = -1) const;

    // ---- Type inspection ---------------------------------------------
    JsonType type() const;
    bool isNull() const { return type() == JsonType::Null; }
    bool isBool() const { return type() == JsonType::Boolean; }
    bool isNumber() const { return type() == JsonType::Number; }
    bool isString() const { return type() == JsonType::String; }
    bool isArray() const { return type() == JsonType::Array; }
    bool isObject() const { return type() == JsonType::Object; }

    // ---- Typed accessors (throw JsonError on type mismatch) ----------
    bool               asBool() const;
    double             asNumber() const;
    long long          asInt() const;
    const std::string& asString() const;
    const JsonArray&   asArray() const;
    JsonArray&         asArray();
    const JsonObject&  asObject() const;
    JsonObject&        asObject();

    // Returns the value if this is the right type, otherwise `fallback`.
    template <typename T>
    T valueOr(const std::string& key, T fallback) const;

    // ---- Object operations -------------------------------------------
    // A null value is automatically converted to an object on first use.
    Json&       operator[](const std::string& key);
    Json&       operator[](const char* key) { return (*this)[std::string(key)]; }
    Json&       at(const std::string& key);
    const Json& at(const std::string& key) const;
    bool        contains(const std::string& key) const;
    bool        erase(const std::string& key);
    std::vector<std::string> keys() const;

    // ---- Array operations --------------------------------------------
    // A null value is automatically converted to an array on push_back.
    Json&       operator[](std::size_t index);
    Json&       operator[](int index) { return (*this)[static_cast<std::size_t>(index)]; }
    Json&       at(std::size_t index);
    const Json& at(std::size_t index) const;
    void        push_back(Json value);
    void        insert(std::size_t index, Json value);
    void        eraseAt(std::size_t index);

    // ---- Generic -----------------------------------------------------
    std::size_t size() const;   // elements / members / 0 for scalars
    bool        empty() const { return size() == 0; }

    // JSON Pointer (RFC 6901): "" = whole document, "/a/0/b" = nested.
    const Json* pointer(const std::string& ptr) const;   // nullptr if absent
    Json*       pointer(const std::string& ptr);

    // Deep merge: objects are merged recursively, everything else replaced.
    void merge(const Json& patch);

    bool operator==(const Json& other) const { return value_ == other.value_; }
    bool operator!=(const Json& other) const { return !(*this == other); }

private:
    std::variant<std::nullptr_t, bool, double, std::string, JsonArray, JsonObject> value_;

    void dumpImpl(std::string& out, int indent, int depth) const;
};

template <typename T>
T Json::valueOr(const std::string& key, T fallback) const {
    if (!isObject() || !contains(key)) return fallback;
    const Json& v = at(key);
    if constexpr (std::is_same_v<T, bool>) {
        return v.isBool() ? v.asBool() : fallback;
    } else if constexpr (std::is_arithmetic_v<T>) {
        return v.isNumber() ? static_cast<T>(v.asNumber()) : fallback;
    } else if constexpr (std::is_convertible_v<std::string, T>) {
        return v.isString() ? T(v.asString()) : fallback;
    } else {
        return fallback;
    }
}

}  // namespace sj

#endif  // SIMPLE_JSON_HPP
