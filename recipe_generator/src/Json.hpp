#pragma once

// Minimal JSON reader/writer, just enough to talk to the Claude Messages API
// without pulling in a third-party dependency.

#include <cstdint>
#include <cstdio>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace json {

struct Value {
    enum class Type { Null, Bool, Number, String, Array, Object } type = Type::Null;
    bool b = false;
    double n = 0;
    std::string s;
    std::vector<Value> a;
    std::map<std::string, Value> o;

    const Value& operator[](const std::string& key) const {
        static const Value kNull;
        if (type != Type::Object) return kNull;
        auto it = o.find(key);
        return it == o.end() ? kNull : it->second;
    }
    bool isNull() const { return type == Type::Null; }
    bool isString() const { return type == Type::String; }
    bool isArray() const { return type == Type::Array; }
};

class Parser {
public:
    explicit Parser(const std::string& text) : t_(text) {}

    Value parse() {
        Value v = value();
        ws();
        if (i_ != t_.size()) fail("trailing characters");
        return v;
    }

private:
    [[noreturn]] void fail(const std::string& why) const {
        throw std::runtime_error("JSON parse error at " + std::to_string(i_) + ": " + why);
    }
    void ws() {
        while (i_ < t_.size() && (t_[i_] == ' ' || t_[i_] == '\n' || t_[i_] == '\r' || t_[i_] == '\t')) ++i_;
    }
    char peek() {
        ws();
        if (i_ >= t_.size()) fail("unexpected end");
        return t_[i_];
    }
    void expect(char c) {
        if (peek() != c) fail(std::string("expected '") + c + "'");
        ++i_;
    }
    bool literal(const char* word) {
        const std::string w(word);
        if (t_.compare(i_, w.size(), w) == 0) { i_ += w.size(); return true; }
        return false;
    }

    Value value() {
        const char c = peek();
        Value v;
        if (c == '{') {
            v.type = Value::Type::Object;
            ++i_;
            if (peek() == '}') { ++i_; return v; }
            for (;;) {
                if (peek() != '"') fail("expected key");
                std::string key = string();
                expect(':');
                v.o[key] = value();
                if (peek() == ',') { ++i_; continue; }
                expect('}');
                return v;
            }
        }
        if (c == '[') {
            v.type = Value::Type::Array;
            ++i_;
            if (peek() == ']') { ++i_; return v; }
            for (;;) {
                v.a.push_back(value());
                if (peek() == ',') { ++i_; continue; }
                expect(']');
                return v;
            }
        }
        if (c == '"') { v.type = Value::Type::String; v.s = string(); return v; }
        if (literal("true"))  { v.type = Value::Type::Bool; v.b = true; return v; }
        if (literal("false")) { v.type = Value::Type::Bool; v.b = false; return v; }
        if (literal("null"))  return v;
        const size_t start = i_;
        while (i_ < t_.size() && std::string("+-0123456789.eE").find(t_[i_]) != std::string::npos) ++i_;
        if (start == i_) fail("unexpected character");
        v.type = Value::Type::Number;
        v.n = std::stod(t_.substr(start, i_ - start));
        return v;
    }

    static void appendUtf8(std::string& out, uint32_t cp) {
        if (cp < 0x80) out += static_cast<char>(cp);
        else if (cp < 0x800) { out += static_cast<char>(0xC0 | (cp >> 6)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (cp >> 18));
            out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }

    uint32_t hex4() {
        if (i_ + 4 > t_.size()) fail("bad \\u escape");
        const uint32_t cp = static_cast<uint32_t>(std::stoul(t_.substr(i_, 4), nullptr, 16));
        i_ += 4;
        return cp;
    }

    std::string string() {
        expect('"');
        std::string out;
        while (i_ < t_.size() && t_[i_] != '"') {
            char c = t_[i_++];
            if (c != '\\') { out += c; continue; }
            if (i_ >= t_.size()) fail("bad escape");
            c = t_[i_++];
            switch (c) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    uint32_t cp = hex4();
                    if (cp >= 0xD800 && cp <= 0xDBFF && t_.compare(i_, 2, "\\u") == 0) {
                        i_ += 2;
                        const uint32_t lo = hex4();
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    }
                    appendUtf8(out, cp);
                    break;
                }
                default: fail("bad escape");
            }
        }
        if (i_ >= t_.size()) fail("unterminated string");
        ++i_;
        return out;
    }

    const std::string& t_;
    size_t i_ = 0;
};

inline Value parse(const std::string& text) { return Parser(text).parse(); }

inline std::string quote(const std::string& s) {
    std::string out = "\"";
    for (unsigned char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof buf, "\\u%04x", c);
                    out += buf;
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
    return out + "\"";
}

} // namespace json
