// json.cpp - Recursive-descent JSON parser and serializer.
#include "json.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace simplechat::json {

// ------------------------------------------------------------------ Value

Value::Value(bool b) : type_(Type::Bool), bool_(b) {}
Value::Value(int n) : type_(Type::Number), text_(std::to_string(n)) {}
Value::Value(double n) : type_(Type::Number) {
    std::ostringstream os;
    os.precision(17);
    os << n;
    text_ = os.str();
}
Value::Value(const char* s) : type_(Type::String), text_(s) {}
Value::Value(std::string s) : type_(Type::String), text_(std::move(s)) {}

Value Value::array() { Value v; v.type_ = Type::Array; return v; }
Value Value::object() { Value v; v.type_ = Type::Object; return v; }
Value Value::numberFromText(std::string text) {
    Value v;
    v.type_ = Type::Number;
    v.text_ = std::move(text);
    return v;
}

const std::string& Value::asString() const {
    static const std::string empty;
    return type_ == Type::String ? text_ : empty;
}

double Value::asNumber() const { return type_ == Type::Number ? std::strtod(text_.c_str(), nullptr) : 0.0; }

void Value::push(Value v) {
    if (type_ != Type::Array) *this = array();
    array_.push_back(std::move(v));
}

Value& Value::set(const std::string& key, Value v) {
    if (type_ != Type::Object) *this = object();
    for (auto& kv : object_)
        if (kv.first == key) { kv.second = std::move(v); return *this; }
    object_.emplace_back(key, std::move(v));
    return *this;
}

const Value* Value::find(const std::string& key) const {
    if (type_ != Type::Object) return nullptr;
    for (const auto& kv : object_)
        if (kv.first == key) return &kv.second;
    return nullptr;
}

const Value& Value::operator[](const std::string& key) const {
    static const Value null;
    const Value* v = find(key);
    return v ? *v : null;
}

// -------------------------------------------------------------- serialize

namespace {

void dumpString(const std::string& s, std::string& out) {
    out += '"';
    for (unsigned char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof buf, "\\u%04x", c);
                    out += buf;
                } else {
                    out += static_cast<char>(c);  // UTF-8 bytes pass through
                }
        }
    }
    out += '"';
}

}  // namespace

void Value::dumpTo(std::string& out) const {
    switch (type_) {
        case Type::Null:   out += "null"; break;
        case Type::Bool:   out += bool_ ? "true" : "false"; break;
        case Type::Number: out += text_; break;
        case Type::String: dumpString(text_, out); break;
        case Type::Array:
            out += '[';
            for (std::size_t i = 0; i < array_.size(); ++i) {
                if (i) out += ',';
                array_[i].dumpTo(out);
            }
            out += ']';
            break;
        case Type::Object:
            out += '{';
            for (std::size_t i = 0; i < object_.size(); ++i) {
                if (i) out += ',';
                dumpString(object_[i].first, out);
                out += ':';
                object_[i].second.dumpTo(out);
            }
            out += '}';
            break;
    }
}

std::string Value::dump() const {
    std::string out;
    dumpTo(out);
    return out;
}

// ------------------------------------------------------------------ parse

namespace {

class Parser {
public:
    explicit Parser(const std::string& s) : s_(s) {}

    Value parseDocument() {
        Value v = parseValue(0);
        skipWs();
        if (pos_ != s_.size()) fail("trailing characters");
        return v;
    }

private:
    const std::string& s_;
    std::size_t pos_ = 0;
    static constexpr int kMaxDepth = 256;

    [[noreturn]] void fail(const std::string& what) const {
        throw ParseError("JSON parse error at offset " + std::to_string(pos_) + ": " + what);
    }
    void skipWs() {
        while (pos_ < s_.size() && (s_[pos_] == ' ' || s_[pos_] == '\t' || s_[pos_] == '\n' || s_[pos_] == '\r'))
            ++pos_;
    }
    char peek() const { return pos_ < s_.size() ? s_[pos_] : '\0'; }
    void expect(char c) {
        if (peek() != c) fail(std::string("expected '") + c + "'");
        ++pos_;
    }
    void expectWord(const char* w) {
        for (const char* p = w; *p; ++p) expect(*p);
    }

    Value parseValue(int depth) {
        if (depth > kMaxDepth) fail("nesting too deep");
        skipWs();
        switch (peek()) {
            case '{': return parseObject(depth);
            case '[': return parseArray(depth);
            case '"': return Value(parseString());
            case 't': expectWord("true"); return Value(true);
            case 'f': expectWord("false"); return Value(false);
            case 'n': expectWord("null"); return Value();
            default:  return parseNumber();
        }
    }

    Value parseObject(int depth) {
        expect('{');
        Value obj = Value::object();
        skipWs();
        if (peek() == '}') { ++pos_; return obj; }
        while (true) {
            skipWs();
            std::string key = parseString();
            skipWs();
            expect(':');
            obj.set(key, parseValue(depth + 1));
            skipWs();
            if (peek() == ',') { ++pos_; continue; }
            expect('}');
            return obj;
        }
    }

    Value parseArray(int depth) {
        expect('[');
        Value arr = Value::array();
        skipWs();
        if (peek() == ']') { ++pos_; return arr; }
        while (true) {
            arr.push(parseValue(depth + 1));
            skipWs();
            if (peek() == ',') { ++pos_; continue; }
            expect(']');
            return arr;
        }
    }

    Value parseNumber() {
        const std::size_t start = pos_;
        if (peek() == '-') ++pos_;
        if (!std::isdigit(static_cast<unsigned char>(peek()))) fail("unexpected character");
        while (std::isdigit(static_cast<unsigned char>(peek())) || peek() == '.' || peek() == 'e' ||
               peek() == 'E' || peek() == '+' || peek() == '-')
            ++pos_;
        return Value::numberFromText(s_.substr(start, pos_ - start));
    }

    unsigned parseHex4() {
        if (pos_ + 4 > s_.size()) fail("bad \\u escape");
        unsigned v = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = s_[pos_++];
            v <<= 4;
            if (c >= '0' && c <= '9') v |= static_cast<unsigned>(c - '0');
            else if (c >= 'a' && c <= 'f') v |= static_cast<unsigned>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= static_cast<unsigned>(c - 'A' + 10);
            else fail("bad hex digit");
        }
        return v;
    }

    static void appendUtf8(std::string& out, unsigned cp) {
        if (cp < 0x80) {
            out += static_cast<char>(cp);
        } else if (cp < 0x800) {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
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

    std::string parseString() {
        expect('"');
        std::string out;
        while (true) {
            if (pos_ >= s_.size()) fail("unterminated string");
            const char c = s_[pos_++];
            if (c == '"') return out;
            if (c != '\\') { out += c; continue; }
            if (pos_ >= s_.size()) fail("unterminated escape");
            switch (s_[pos_++]) {
                case '"':  out += '"';  break;
                case '\\': out += '\\'; break;
                case '/':  out += '/';  break;
                case 'b':  out += '\b'; break;
                case 'f':  out += '\f'; break;
                case 'n':  out += '\n'; break;
                case 'r':  out += '\r'; break;
                case 't':  out += '\t'; break;
                case 'u': {
                    unsigned cp = parseHex4();
                    if (cp >= 0xD800 && cp <= 0xDBFF && pos_ + 1 < s_.size() && s_[pos_] == '\\' &&
                        s_[pos_ + 1] == 'u') {
                        pos_ += 2;
                        const unsigned lo = parseHex4();
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    }
                    appendUtf8(out, cp);
                    break;
                }
                default: fail("bad escape");
            }
        }
    }
};

}  // namespace

Value parse(const std::string& text) { return Parser(text).parseDocument(); }

}  // namespace simplechat::json
