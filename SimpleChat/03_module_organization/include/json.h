// json.h - A tiny, dependency-free JSON value with parse() and dump().
//
// Just enough for the Claude Messages API: objects keep key order, and
// numbers keep their original text so values round-trip unchanged
// (assistant content is sent back to the API exactly as received).
#pragma once
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace simplechat::json {

struct ParseError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

class Value {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    Value() = default;                       // null
    Value(bool b);                           // NOLINT(google-explicit-constructor)
    Value(int n);                            // NOLINT
    Value(double n);                         // NOLINT
    Value(const char* s);                    // NOLINT
    Value(std::string s);                    // NOLINT
    static Value array();
    static Value object();
    static Value numberFromText(std::string text);

    Type type() const { return type_; }
    bool isNull() const { return type_ == Type::Null; }
    bool isString() const { return type_ == Type::String; }
    bool isArray() const { return type_ == Type::Array; }
    bool isObject() const { return type_ == Type::Object; }

    // Accessors (return a default when the type does not match).
    const std::string& asString() const;
    bool asBool() const { return type_ == Type::Bool && bool_; }
    double asNumber() const;

    // Arrays
    void push(Value v);
    const std::vector<Value>& items() const { return array_; }

    // Objects
    Value& set(const std::string& key, Value v);  // insert or replace
    const Value* find(const std::string& key) const;
    // Convenience: obj["a"] returns a null Value when missing.
    const Value& operator[](const std::string& key) const;

    std::string dump() const;

private:
    Type type_ = Type::Null;
    bool bool_ = false;
    std::string text_;  // string contents, or the literal text of a number
    std::vector<Value> array_;
    std::vector<std::pair<std::string, Value>> object_;

    void dumpTo(std::string& out) const;
};

Value parse(const std::string& text);  // throws ParseError

}  // namespace simplechat::json
