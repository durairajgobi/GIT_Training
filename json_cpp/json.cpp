// json.cpp - Implementation of the sj::Json library.

#include "json.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace sj {

// ======================================================================
//  Helpers
// ======================================================================

const char* typeName(JsonType t) {
    switch (t) {
        case JsonType::Null:    return "null";
        case JsonType::Boolean: return "boolean";
        case JsonType::Number:  return "number";
        case JsonType::String:  return "string";
        case JsonType::Array:   return "array";
        case JsonType::Object:  return "object";
    }
    return "unknown";
}

static JsonError typeError(const char* expected, JsonType actual) {
    return JsonError(std::string("type error: expected ") + expected + ", got " +
                     typeName(actual));
}

// ======================================================================
//  JsonObject
// ======================================================================

JsonObject::iterator JsonObject::find(const std::string& key) {
    for (auto it = members_.begin(); it != members_.end(); ++it)
        if (it->first == key) return it;
    return members_.end();
}

JsonObject::const_iterator JsonObject::find(const std::string& key) const {
    for (auto it = members_.begin(); it != members_.end(); ++it)
        if (it->first == key) return it;
    return members_.end();
}

Json& JsonObject::operator[](const std::string& key) {
    auto it = find(key);
    if (it != members_.end()) return it->second;
    members_.emplace_back(key, Json());
    return members_.back().second;
}

Json& JsonObject::at(const std::string& key) {
    auto it = find(key);
    if (it == members_.end()) throw JsonError("key not found: \"" + key + "\"");
    return it->second;
}

const Json& JsonObject::at(const std::string& key) const {
    auto it = find(key);
    if (it == members_.end()) throw JsonError("key not found: \"" + key + "\"");
    return it->second;
}

bool JsonObject::contains(const std::string& key) const { return find(key) != members_.end(); }

bool JsonObject::erase(const std::string& key) {
    auto it = find(key);
    if (it == members_.end()) return false;
    members_.erase(it);
    return true;
}

// Two objects are equal if they have the same keys with equal values,
// regardless of key order.
bool JsonObject::operator==(const JsonObject& other) const {
    if (size() != other.size()) return false;
    for (const auto& m : members_) {
        auto it = other.find(m.first);
        if (it == other.members_.end() || it->second != m.second) return false;
    }
    return true;
}

// ======================================================================
//  Json: factories, type inspection, accessors
// ======================================================================

Json Json::array(std::initializer_list<Json> items) { return Json(JsonArray(items)); }

Json Json::object(std::initializer_list<JsonMember> members) {
    JsonObject obj;
    for (const auto& m : members) obj[m.first] = m.second;
    return Json(std::move(obj));
}

JsonType Json::type() const { return static_cast<JsonType>(value_.index()); }

bool Json::asBool() const {
    if (!isBool()) throw typeError("boolean", type());
    return std::get<bool>(value_);
}

double Json::asNumber() const {
    if (!isNumber()) throw typeError("number", type());
    return std::get<double>(value_);
}

long long Json::asInt() const { return static_cast<long long>(asNumber()); }

const std::string& Json::asString() const {
    if (!isString()) throw typeError("string", type());
    return std::get<std::string>(value_);
}

const JsonArray& Json::asArray() const {
    if (!isArray()) throw typeError("array", type());
    return std::get<JsonArray>(value_);
}

JsonArray& Json::asArray() {
    if (!isArray()) throw typeError("array", type());
    return std::get<JsonArray>(value_);
}

const JsonObject& Json::asObject() const {
    if (!isObject()) throw typeError("object", type());
    return std::get<JsonObject>(value_);
}

JsonObject& Json::asObject() {
    if (!isObject()) throw typeError("object", type());
    return std::get<JsonObject>(value_);
}

// ======================================================================
//  Object operations
// ======================================================================

Json& Json::operator[](const std::string& key) {
    if (isNull()) value_ = JsonObject();
    return asObject()[key];
}

Json&       Json::at(const std::string& key) { return asObject().at(key); }
const Json& Json::at(const std::string& key) const { return asObject().at(key); }

bool Json::contains(const std::string& key) const {
    return isObject() && asObject().contains(key);
}

bool Json::erase(const std::string& key) { return asObject().erase(key); }

std::vector<std::string> Json::keys() const {
    std::vector<std::string> result;
    for (const auto& m : asObject()) result.push_back(m.first);
    return result;
}

// ======================================================================
//  Array operations
// ======================================================================

Json& Json::operator[](std::size_t index) { return at(index); }

Json& Json::at(std::size_t index) {
    JsonArray& arr = asArray();
    if (index >= arr.size())
        throw JsonError("array index " + std::to_string(index) + " out of range (size " +
                        std::to_string(arr.size()) + ")");
    return arr[index];
}

const Json& Json::at(std::size_t index) const {
    const JsonArray& arr = asArray();
    if (index >= arr.size())
        throw JsonError("array index " + std::to_string(index) + " out of range (size " +
                        std::to_string(arr.size()) + ")");
    return arr[index];
}

void Json::push_back(Json value) {
    if (isNull()) value_ = JsonArray();
    asArray().push_back(std::move(value));
}

void Json::insert(std::size_t index, Json value) {
    JsonArray& arr = asArray();
    if (index > arr.size()) throw JsonError("insert index out of range");
    arr.insert(arr.begin() + static_cast<std::ptrdiff_t>(index), std::move(value));
}

void Json::eraseAt(std::size_t index) {
    JsonArray& arr = asArray();
    if (index >= arr.size()) throw JsonError("erase index out of range");
    arr.erase(arr.begin() + static_cast<std::ptrdiff_t>(index));
}

std::size_t Json::size() const {
    if (isArray()) return asArray().size();
    if (isObject()) return asObject().size();
    return 0;
}

// ======================================================================
//  JSON Pointer & merge
// ======================================================================

const Json* Json::pointer(const std::string& ptr) const {
    if (ptr.empty()) return this;
    if (ptr[0] != '/') throw JsonError("JSON pointer must start with '/': " + ptr);

    const Json* cur = this;
    std::size_t pos = 1;
    while (true) {
        std::size_t next  = ptr.find('/', pos);
        std::string token = ptr.substr(pos, next == std::string::npos ? std::string::npos
                                                                      : next - pos);
        // Unescape ~1 -> '/' and ~0 -> '~' (order matters).
        std::string key;
        for (std::size_t i = 0; i < token.size(); ++i) {
            if (token[i] == '~' && i + 1 < token.size() && token[i + 1] == '1') { key += '/'; ++i; }
            else if (token[i] == '~' && i + 1 < token.size() && token[i + 1] == '0') { key += '~'; ++i; }
            else key += token[i];
        }

        if (cur->isObject()) {
            if (!cur->contains(key)) return nullptr;
            cur = &cur->at(key);
        } else if (cur->isArray()) {
            if (key.empty() || key.find_first_not_of("0123456789") != std::string::npos)
                return nullptr;
            std::size_t idx = std::stoul(key);
            if (idx >= cur->size()) return nullptr;
            cur = &cur->at(idx);
        } else {
            return nullptr;
        }

        if (next == std::string::npos) return cur;
        pos = next + 1;
    }
}

Json* Json::pointer(const std::string& ptr) {
    return const_cast<Json*>(static_cast<const Json*>(this)->pointer(ptr));
}

void Json::merge(const Json& patch) {
    if (!patch.isObject() || !isObject()) {
        *this = patch;
        return;
    }
    for (const auto& m : patch.asObject()) {
        Json& target = (*this)[m.first];
        if (target.isObject() && m.second.isObject())
            target.merge(m.second);
        else
            target = m.second;
    }
}

// ======================================================================
//  Serialization
// ======================================================================

static void escapeString(std::string& out, const std::string& s) {
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
                    out += static_cast<char>(c);   // UTF-8 passes through unchanged
                }
        }
    }
    out += '"';
}

static void writeNumber(std::string& out, double d) {
    if (!std::isfinite(d)) {           // JSON has no NaN / Infinity
        out += "null";
        return;
    }
    char buf[32];
    if (d == std::floor(d) && std::fabs(d) < 1e15) {
        std::snprintf(buf, sizeof buf, "%.0f", d);           // integral value
    } else {
        std::snprintf(buf, sizeof buf, "%.17g", d);          // full precision
        // Use the shortest representation that round-trips.
        for (int prec = 1; prec <= 17; ++prec) {
            char tmp[32];
            std::snprintf(tmp, sizeof tmp, "%.*g", prec, d);
            if (std::strtod(tmp, nullptr) == d) {
                std::snprintf(buf, sizeof buf, "%s", tmp);
                break;
            }
        }
    }
    out += buf;
}

static void newline(std::string& out, int indent, int depth) {
    if (indent < 0) return;
    out += '\n';
    out.append(static_cast<std::size_t>(indent * depth), ' ');
}

void Json::dumpImpl(std::string& out, int indent, int depth) const {
    switch (type()) {
        case JsonType::Null:    out += "null"; break;
        case JsonType::Boolean: out += asBool() ? "true" : "false"; break;
        case JsonType::Number:  writeNumber(out, asNumber()); break;
        case JsonType::String:  escapeString(out, asString()); break;

        case JsonType::Array: {
            const JsonArray& arr = asArray();
            if (arr.empty()) { out += "[]"; break; }
            out += '[';
            for (std::size_t i = 0; i < arr.size(); ++i) {
                if (i) out += ',';
                newline(out, indent, depth + 1);
                arr[i].dumpImpl(out, indent, depth + 1);
            }
            newline(out, indent, depth);
            out += ']';
            break;
        }

        case JsonType::Object: {
            const JsonObject& obj = asObject();
            if (obj.empty()) { out += "{}"; break; }
            out += '{';
            bool first = true;
            for (const auto& m : obj) {
                if (!first) out += ',';
                first = false;
                newline(out, indent, depth + 1);
                escapeString(out, m.first);
                out += indent < 0 ? ":" : ": ";
                m.second.dumpImpl(out, indent, depth + 1);
            }
            newline(out, indent, depth);
            out += '}';
            break;
        }
    }
}

std::string Json::dump(int indent) const {
    std::string out;
    dumpImpl(out, indent, 0);
    return out;
}

// ======================================================================
//  Parser (recursive descent)
// ======================================================================

namespace {

class Parser {
public:
    explicit Parser(const std::string& text) : s_(text) {}

    Json parseDocument() {
        skipWs();
        Json v = parseValue(0);
        skipWs();
        if (pos_ != s_.size()) fail("unexpected trailing characters");
        return v;
    }

private:
    static constexpr int kMaxDepth = 512;   // guard against stack overflow
    const std::string& s_;
    std::size_t pos_ = 0;

    [[noreturn]] void fail(const std::string& msg) const {
        // Compute line / column for a helpful error message.
        std::size_t line = 1, col = 1;
        for (std::size_t i = 0; i < pos_ && i < s_.size(); ++i) {
            if (s_[i] == '\n') { ++line; col = 1; } else { ++col; }
        }
        throw JsonError("parse error at line " + std::to_string(line) + ", column " +
                        std::to_string(col) + ": " + msg);
    }

    bool atEnd() const { return pos_ >= s_.size(); }
    char peek() const { return atEnd() ? '\0' : s_[pos_]; }

    void skipWs() {
        while (!atEnd() && (s_[pos_] == ' ' || s_[pos_] == '\t' ||
                            s_[pos_] == '\n' || s_[pos_] == '\r'))
            ++pos_;
    }

    void expect(char c) {
        if (peek() != c) fail(std::string("expected '") + c + "'");
        ++pos_;
    }

    void expectLiteral(const char* lit) {
        for (const char* p = lit; *p; ++p) {
            if (peek() != *p) fail(std::string("invalid literal, expected '") + lit + "'");
            ++pos_;
        }
    }

    Json parseValue(int depth) {
        if (depth > kMaxDepth) fail("nesting too deep");
        switch (peek()) {
            case '{': return parseObject(depth);
            case '[': return parseArray(depth);
            case '"': return Json(parseString());
            case 't': expectLiteral("true");  return Json(true);
            case 'f': expectLiteral("false"); return Json(false);
            case 'n': expectLiteral("null");  return Json(nullptr);
            default:
                if (peek() == '-' || (peek() >= '0' && peek() <= '9')) return parseNumber();
                if (atEnd()) fail("unexpected end of input");
                fail(std::string("unexpected character '") + peek() + "'");
        }
    }

    Json parseObject(int depth) {
        expect('{');
        JsonObject obj;
        skipWs();
        if (peek() == '}') { ++pos_; return Json(std::move(obj)); }
        while (true) {
            skipWs();
            if (peek() != '"') fail("expected string key");
            std::string key = parseString();
            skipWs();
            expect(':');
            skipWs();
            obj[key] = parseValue(depth + 1);   // duplicate keys: last one wins
            skipWs();
            if (peek() == ',') { ++pos_; continue; }
            if (peek() == '}') { ++pos_; break; }
            fail("expected ',' or '}' in object");
        }
        return Json(std::move(obj));
    }

    Json parseArray(int depth) {
        expect('[');
        JsonArray arr;
        skipWs();
        if (peek() == ']') { ++pos_; return Json(std::move(arr)); }
        while (true) {
            skipWs();
            arr.push_back(parseValue(depth + 1));
            skipWs();
            if (peek() == ',') { ++pos_; continue; }
            if (peek() == ']') { ++pos_; break; }
            fail("expected ',' or ']' in array");
        }
        return Json(std::move(arr));
    }

    Json parseNumber() {
        std::size_t start = pos_;
        auto isDigit = [this] { return peek() >= '0' && peek() <= '9'; };

        if (peek() == '-') ++pos_;
        if (peek() == '0') {
            ++pos_;                                   // no leading zeros allowed
        } else if (isDigit()) {
            while (isDigit()) ++pos_;
        } else {
            fail("invalid number");
        }
        if (peek() == '.') {
            ++pos_;
            if (!isDigit()) fail("expected digit after decimal point");
            while (isDigit()) ++pos_;
        }
        if (peek() == 'e' || peek() == 'E') {
            ++pos_;
            if (peek() == '+' || peek() == '-') ++pos_;
            if (!isDigit()) fail("expected digit in exponent");
            while (isDigit()) ++pos_;
        }
        return Json(std::strtod(s_.substr(start, pos_ - start).c_str(), nullptr));
    }

    unsigned parseHex4() {
        unsigned v = 0;
        for (int i = 0; i < 4; ++i) {
            char c = peek();
            v <<= 4;
            if (c >= '0' && c <= '9')      v |= static_cast<unsigned>(c - '0');
            else if (c >= 'a' && c <= 'f') v |= static_cast<unsigned>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= static_cast<unsigned>(c - 'A' + 10);
            else fail("invalid \\u escape");
            ++pos_;
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
            if (atEnd()) fail("unterminated string");
            char c = s_[pos_++];
            if (c == '"') break;
            if (static_cast<unsigned char>(c) < 0x20) {
                --pos_;
                fail("control character in string must be escaped");
            }
            if (c != '\\') { out += c; continue; }

            if (atEnd()) fail("unterminated escape sequence");
            char e = s_[pos_++];
            switch (e) {
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
                    if (cp >= 0xD800 && cp <= 0xDBFF) {          // high surrogate
                        if (peek() != '\\') fail("unpaired surrogate");
                        ++pos_;
                        if (peek() != 'u') fail("unpaired surrogate");
                        ++pos_;
                        unsigned lo = parseHex4();
                        if (lo < 0xDC00 || lo > 0xDFFF) fail("invalid low surrogate");
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                        fail("unpaired low surrogate");
                    }
                    appendUtf8(out, cp);
                    break;
                }
                default:
                    --pos_;
                    fail(std::string("invalid escape '\\") + e + "'");
            }
        }
        return out;
    }
};

}  // namespace

Json Json::parse(const std::string& text) { return Parser(text).parseDocument(); }

// ======================================================================
//  File I/O
// ======================================================================

Json Json::loadFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw JsonError("cannot open file for reading: " + path);
    std::ostringstream ss;
    ss << in.rdbuf();
    return parse(ss.str());
}

void Json::saveFile(const std::string& path, int indent) const {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) throw JsonError("cannot open file for writing: " + path);
    out << dump(indent) << '\n';
    if (!out) throw JsonError("failed writing file: " + path);
}

}  // namespace sj
