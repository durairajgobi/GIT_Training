#include "wa/json.hpp"

#include <cmath>
#include <cstdio>
#include <sstream>

namespace wa {

bool Json::asBool() const {
    if (!isBool()) throw JsonError("JSON value is not a bool");
    return std::get<bool>(value_);
}
double Json::asNumber() const {
    if (!isNumber()) throw JsonError("JSON value is not a number");
    return std::get<double>(value_);
}
const std::string& Json::asString() const {
    if (!isString()) throw JsonError("JSON value is not a string");
    return std::get<std::string>(value_);
}
const Json::Array& Json::asArray() const {
    if (!isArray()) throw JsonError("JSON value is not an array");
    return std::get<Array>(value_);
}
Json::Array& Json::asArray() {
    if (!isArray()) throw JsonError("JSON value is not an array");
    return std::get<Array>(value_);
}
const Json::Object& Json::asObject() const {
    if (!isObject()) throw JsonError("JSON value is not an object");
    return std::get<Object>(value_);
}
Json::Object& Json::asObject() {
    if (!isObject()) throw JsonError("JSON value is not an object");
    return std::get<Object>(value_);
}

Json& Json::operator[](const std::string& key) {
    if (isNull()) value_ = Object{};
    return asObject()[key];
}

const Json* Json::find(const std::string& key) const {
    if (!isObject()) return nullptr;
    const auto& obj = std::get<Object>(value_);
    auto it = obj.find(key);
    return it == obj.end() ? nullptr : &it->second;
}

std::string Json::getString(const std::string& key, const std::string& fallback) const {
    const Json* v = find(key);
    return (v && v->isString()) ? v->asString() : fallback;
}

void Json::push_back(Json v) {
    if (isNull()) value_ = Array{};
    asArray().push_back(std::move(v));
}

std::string jsonEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 2);
    for (unsigned char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
    return out;
}

namespace {

void dumpTo(const Json& j, std::string& out) {
    if (j.isNull()) {
        out += "null";
    } else if (j.isBool()) {
        out += j.asBool() ? "true" : "false";
    } else if (j.isNumber()) {
        double d = j.asNumber();
        if (!std::isfinite(d)) throw JsonError("cannot serialize non-finite number");
        if (d == std::floor(d) && std::fabs(d) < 1e15) {
            out += std::to_string(static_cast<long long>(d));
        } else {
            std::ostringstream os;
            os.precision(17);
            os << d;
            out += os.str();
        }
    } else if (j.isString()) {
        out += '"';
        out += jsonEscape(j.asString());
        out += '"';
    } else if (j.isArray()) {
        out += '[';
        bool first = true;
        for (const auto& v : j.asArray()) {
            if (!first) out += ',';
            first = false;
            dumpTo(v, out);
        }
        out += ']';
    } else {
        out += '{';
        bool first = true;
        for (const auto& [k, v] : j.asObject()) {
            if (!first) out += ',';
            first = false;
            out += '"';
            out += jsonEscape(k);
            out += "\":";
            dumpTo(v, out);
        }
        out += '}';
    }
}

class Parser {
public:
    explicit Parser(const std::string& s) : s_(s) {}

    Json parseDocument() {
        Json v = parseValue(0);
        skipWs();
        if (pos_ != s_.size()) fail("trailing characters");
        return v;
    }

private:
    static constexpr int kMaxDepth = 256;
    const std::string& s_;
    size_t pos_ = 0;

    [[noreturn]] void fail(const std::string& msg) const {
        throw JsonError("JSON parse error at offset " + std::to_string(pos_) + ": " + msg);
    }
    void skipWs() {
        while (pos_ < s_.size() &&
               (s_[pos_] == ' ' || s_[pos_] == '\n' || s_[pos_] == '\r' || s_[pos_] == '\t'))
            ++pos_;
    }
    char peek() {
        skipWs();
        if (pos_ >= s_.size()) fail("unexpected end of input");
        return s_[pos_];
    }
    void expectLiteral(const char* lit) {
        for (const char* p = lit; *p; ++p) {
            if (pos_ >= s_.size() || s_[pos_] != *p) fail(std::string("expected '") + lit + "'");
            ++pos_;
        }
    }

    Json parseValue(int depth) {
        if (depth > kMaxDepth) fail("nesting too deep");
        char c = peek();
        switch (c) {
            case '{': return parseObject(depth);
            case '[': return parseArray(depth);
            case '"': return Json(parseString());
            case 't': expectLiteral("true"); return Json(true);
            case 'f': expectLiteral("false"); return Json(false);
            case 'n': expectLiteral("null"); return Json(nullptr);
            default:
                if (c == '-' || (c >= '0' && c <= '9')) return parseNumber();
                fail(std::string("unexpected character '") + c + "'");
        }
    }

    Json parseObject(int depth) {
        ++pos_;  // '{'
        Json::Object obj;
        if (peek() == '}') { ++pos_; return Json(std::move(obj)); }
        while (true) {
            if (peek() != '"') fail("expected string key");
            std::string key = parseString();
            if (peek() != ':') fail("expected ':'");
            ++pos_;
            obj[key] = parseValue(depth + 1);
            char c = peek();
            ++pos_;
            if (c == '}') break;
            if (c != ',') fail("expected ',' or '}'");
        }
        return Json(std::move(obj));
    }

    Json parseArray(int depth) {
        ++pos_;  // '['
        Json::Array arr;
        if (peek() == ']') { ++pos_; return Json(std::move(arr)); }
        while (true) {
            arr.push_back(parseValue(depth + 1));
            char c = peek();
            ++pos_;
            if (c == ']') break;
            if (c != ',') fail("expected ',' or ']'");
        }
        return Json(std::move(arr));
    }

    Json parseNumber() {
        size_t start = pos_;
        if (s_[pos_] == '-') ++pos_;
        auto digits = [&] {
            size_t d = pos_;
            while (pos_ < s_.size() && s_[pos_] >= '0' && s_[pos_] <= '9') ++pos_;
            if (pos_ == d) fail("expected digit");
        };
        digits();
        if (pos_ < s_.size() && s_[pos_] == '.') { ++pos_; digits(); }
        if (pos_ < s_.size() && (s_[pos_] == 'e' || s_[pos_] == 'E')) {
            ++pos_;
            if (pos_ < s_.size() && (s_[pos_] == '+' || s_[pos_] == '-')) ++pos_;
            digits();
        }
        return Json(std::stod(s_.substr(start, pos_ - start)));
    }

    unsigned parseHex4() {
        if (pos_ + 4 > s_.size()) fail("truncated \\u escape");
        unsigned v = 0;
        for (int i = 0; i < 4; ++i) {
            char c = s_[pos_++];
            v <<= 4;
            if (c >= '0' && c <= '9') v |= c - '0';
            else if (c >= 'a' && c <= 'f') v |= c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') v |= c - 'A' + 10;
            else fail("invalid hex digit");
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
        ++pos_;  // opening quote
        std::string out;
        while (true) {
            if (pos_ >= s_.size()) fail("unterminated string");
            char c = s_[pos_++];
            if (c == '"') break;
            if (static_cast<unsigned char>(c) < 0x20) fail("control character in string");
            if (c != '\\') { out += c; continue; }
            if (pos_ >= s_.size()) fail("unterminated escape");
            char e = s_[pos_++];
            switch (e) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    unsigned cp = parseHex4();
                    if (cp >= 0xD800 && cp <= 0xDBFF) {
                        if (pos_ + 2 <= s_.size() && s_[pos_] == '\\' && s_[pos_ + 1] == 'u') {
                            pos_ += 2;
                            unsigned lo = parseHex4();
                            if (lo < 0xDC00 || lo > 0xDFFF) fail("invalid surrogate pair");
                            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                        } else {
                            fail("lone high surrogate");
                        }
                    }
                    appendUtf8(out, cp);
                    break;
                }
                default: fail("invalid escape");
            }
        }
        return out;
    }
};

}  // namespace

std::string Json::dump() const {
    std::string out;
    dumpTo(*this, out);
    return out;
}

Json Json::parse(const std::string& text) { return Parser(text).parseDocument(); }

}  // namespace wa
