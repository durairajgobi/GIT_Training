#include "wifimgr/json.hpp"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace wifimgr {
namespace {

const std::string kEmptyString;
const Json::Array kEmptyArray;
const Json::Object kEmptyObject;

// Length of a valid UTF-8 sequence starting at s[i], or 0 if invalid.
size_t utf8SeqLen(std::string_view s, size_t i) {
  unsigned char c = static_cast<unsigned char>(s[i]);
  size_t len;
  if (c < 0x80) return 1;
  if ((c & 0xe0) == 0xc0 && c >= 0xc2) len = 2;
  else if ((c & 0xf0) == 0xe0) len = 3;
  else if ((c & 0xf8) == 0xf0 && c <= 0xf4) len = 4;
  else return 0;
  if (i + len > s.size()) return 0;
  for (size_t k = 1; k < len; ++k)
    if ((static_cast<unsigned char>(s[i + k]) & 0xc0) != 0x80) return 0;
  return len;
}

void appendUtf8(std::string& out, uint32_t cp) {
  if (cp < 0x80) {
    out.push_back(static_cast<char>(cp));
  } else if (cp < 0x800) {
    out.push_back(static_cast<char>(0xc0 | (cp >> 6)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
  } else if (cp < 0x10000) {
    out.push_back(static_cast<char>(0xe0 | (cp >> 12)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
  } else {
    out.push_back(static_cast<char>(0xf0 | (cp >> 18)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3f)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
  }
}

// SSIDs are arbitrary bytes. Valid UTF-8 passes through; any other byte is
// emitted as \u00XX so the output is always valid JSON.
void dumpString(std::string& out, std::string_view s) {
  out.push_back('"');
  for (size_t i = 0; i < s.size();) {
    unsigned char c = static_cast<unsigned char>(s[i]);
    switch (c) {
      case '"': out.append("\\\""); ++i; continue;
      case '\\': out.append("\\\\"); ++i; continue;
      case '\n': out.append("\\n"); ++i; continue;
      case '\r': out.append("\\r"); ++i; continue;
      case '\t': out.append("\\t"); ++i; continue;
      default: break;
    }
    size_t len = utf8SeqLen(s, i);
    if (c < 0x20 || len == 0) {
      char buf[8];
      std::snprintf(buf, sizeof buf, "\\u%04x", c);
      out.append(buf);
      ++i;
    } else {
      out.append(s.substr(i, len));
      i += len;
    }
  }
  out.push_back('"');
}

class Parser {
 public:
  explicit Parser(std::string_view t) : t_(t) {}

  std::optional<Json> parseDocument() {
    auto v = parseValue(0);
    if (!v) return std::nullopt;
    skipWs();
    if (pos_ != t_.size()) return std::nullopt;
    return v;
  }

 private:
  void skipWs() {
    while (pos_ < t_.size() &&
           (t_[pos_] == ' ' || t_[pos_] == '\t' || t_[pos_] == '\n' || t_[pos_] == '\r'))
      ++pos_;
  }
  bool consume(std::string_view lit) {
    if (t_.substr(pos_, lit.size()) != lit) return false;
    pos_ += lit.size();
    return true;
  }

  std::optional<Json> parseValue(int depth) {
    if (depth > 64) return std::nullopt;
    skipWs();
    if (pos_ >= t_.size()) return std::nullopt;
    char c = t_[pos_];
    if (c == '{') return parseObject(depth);
    if (c == '[') return parseArray(depth);
    if (c == '"') {
      auto s = parseString();
      if (!s) return std::nullopt;
      return Json(std::move(*s));
    }
    if (consume("true")) return Json(true);
    if (consume("false")) return Json(false);
    if (consume("null")) return Json();
    return parseNumber();
  }

  std::optional<Json> parseNumber() {
    size_t start = pos_;
    if (pos_ < t_.size() && t_[pos_] == '-') ++pos_;
    while (pos_ < t_.size() &&
           (std::isdigit(static_cast<unsigned char>(t_[pos_])) || t_[pos_] == '.' ||
            t_[pos_] == 'e' || t_[pos_] == 'E' || t_[pos_] == '+' || t_[pos_] == '-'))
      ++pos_;
    if (start == pos_) return std::nullopt;
    std::string num(t_.substr(start, pos_ - start));
    char* end = nullptr;
    double v = std::strtod(num.c_str(), &end);
    if (*end != '\0') return std::nullopt;
    return Json(v);
  }

  std::optional<uint32_t> parseHex4() {
    if (pos_ + 4 > t_.size()) return std::nullopt;
    uint32_t v = 0;
    for (int k = 0; k < 4; ++k) {
      char c = t_[pos_++];
      v <<= 4;
      if (c >= '0' && c <= '9') v |= c - '0';
      else if (c >= 'a' && c <= 'f') v |= c - 'a' + 10;
      else if (c >= 'A' && c <= 'F') v |= c - 'A' + 10;
      else return std::nullopt;
    }
    return v;
  }

  std::optional<std::string> parseString() {
    ++pos_;  // opening quote
    std::string out;
    while (pos_ < t_.size()) {
      char c = t_[pos_++];
      if (c == '"') return out;
      if (c != '\\') {
        out.push_back(c);
        continue;
      }
      if (pos_ >= t_.size()) return std::nullopt;
      char e = t_[pos_++];
      switch (e) {
        case '"': out.push_back('"'); break;
        case '\\': out.push_back('\\'); break;
        case '/': out.push_back('/'); break;
        case 'b': out.push_back('\b'); break;
        case 'f': out.push_back('\f'); break;
        case 'n': out.push_back('\n'); break;
        case 'r': out.push_back('\r'); break;
        case 't': out.push_back('\t'); break;
        case 'u': {
          auto cp = parseHex4();
          if (!cp) return std::nullopt;
          uint32_t code = *cp;
          if (code >= 0xd800 && code < 0xdc00 && consume("\\u")) {
            auto lo = parseHex4();
            if (!lo || *lo < 0xdc00 || *lo > 0xdfff) return std::nullopt;
            code = 0x10000 + ((code - 0xd800) << 10) + (*lo - 0xdc00);
          }
          // \u0080-ÿ are produced by dumpString() for raw (non UTF-8)
          // bytes; map them back to the original byte.
          if (code >= 0x80 && code <= 0xff) out.push_back(static_cast<char>(code));
          else appendUtf8(out, code);
          break;
        }
        default: return std::nullopt;
      }
    }
    return std::nullopt;
  }

  std::optional<Json> parseArray(int depth) {
    ++pos_;
    Json arr = Json::array();
    skipWs();
    if (pos_ < t_.size() && t_[pos_] == ']') {
      ++pos_;
      return arr;
    }
    for (;;) {
      auto v = parseValue(depth + 1);
      if (!v) return std::nullopt;
      arr.push(std::move(*v));
      skipWs();
      if (pos_ >= t_.size()) return std::nullopt;
      if (t_[pos_] == ',') {
        ++pos_;
        continue;
      }
      if (t_[pos_] == ']') {
        ++pos_;
        return arr;
      }
      return std::nullopt;
    }
  }

  std::optional<Json> parseObject(int depth) {
    ++pos_;
    Json obj = Json::object();
    skipWs();
    if (pos_ < t_.size() && t_[pos_] == '}') {
      ++pos_;
      return obj;
    }
    for (;;) {
      skipWs();
      if (pos_ >= t_.size() || t_[pos_] != '"') return std::nullopt;
      auto key = parseString();
      if (!key) return std::nullopt;
      skipWs();
      if (pos_ >= t_.size() || t_[pos_] != ':') return std::nullopt;
      ++pos_;
      auto v = parseValue(depth + 1);
      if (!v) return std::nullopt;
      obj.set(*key, std::move(*v));
      skipWs();
      if (pos_ >= t_.size()) return std::nullopt;
      if (t_[pos_] == ',') {
        ++pos_;
        continue;
      }
      if (t_[pos_] == '}') {
        ++pos_;
        return obj;
      }
      return std::nullopt;
    }
  }

  std::string_view t_;
  size_t pos_ = 0;
};

}  // namespace

std::optional<Json> Json::parse(std::string_view text) { return Parser(text).parseDocument(); }

const std::string& Json::asString() const {
  return type_ == Type::String ? str_ : kEmptyString;
}
const Json::Array& Json::items() const { return type_ == Type::Array ? arr_ : kEmptyArray; }
const Json::Object& Json::members() const {
  return type_ == Type::Object ? obj_ : kEmptyObject;
}

Json& Json::set(const std::string& key, Json value) {
  if (type_ == Type::Null) type_ = Type::Object;
  for (auto& kv : obj_) {
    if (kv.first == key) {
      kv.second = std::move(value);
      return *this;
    }
  }
  obj_.emplace_back(key, std::move(value));
  return *this;
}

Json& Json::push(Json value) {
  if (type_ == Type::Null) type_ = Type::Array;
  arr_.push_back(std::move(value));
  return *this;
}

const Json* Json::get(std::string_view key) const {
  if (type_ != Type::Object) return nullptr;
  for (const auto& kv : obj_)
    if (kv.first == key) return &kv.second;
  return nullptr;
}

std::string Json::getString(std::string_view key, const std::string& def) const {
  const Json* v = get(key);
  return v && v->isString() ? v->str_ : def;
}
long Json::getInt(std::string_view key, long def) const {
  const Json* v = get(key);
  return v ? v->asInt(def) : def;
}
bool Json::getBool(std::string_view key, bool def) const {
  const Json* v = get(key);
  return v ? v->asBool(def) : def;
}

std::string Json::dump() const {
  std::string out;
  dumpTo(out);
  return out;
}

void Json::dumpTo(std::string& out) const {
  switch (type_) {
    case Type::Null: out.append("null"); break;
    case Type::Bool: out.append(bool_ ? "true" : "false"); break;
    case Type::Number: {
      char buf[32];
      if (std::isfinite(num_) && num_ == std::floor(num_) && std::fabs(num_) < 1e15)
        std::snprintf(buf, sizeof buf, "%lld", static_cast<long long>(num_));
      else if (std::isfinite(num_))
        std::snprintf(buf, sizeof buf, "%.17g", num_);
      else
        std::snprintf(buf, sizeof buf, "null");
      out.append(buf);
      break;
    }
    case Type::String: dumpString(out, str_); break;
    case Type::Array:
      out.push_back('[');
      for (size_t i = 0; i < arr_.size(); ++i) {
        if (i) out.push_back(',');
        arr_[i].dumpTo(out);
      }
      out.push_back(']');
      break;
    case Type::Object:
      out.push_back('{');
      for (size_t i = 0; i < obj_.size(); ++i) {
        if (i) out.push_back(',');
        dumpString(out, obj_[i].first);
        out.push_back(':');
        obj_[i].second.dumpTo(out);
      }
      out.push_back('}');
      break;
  }
}

}  // namespace wifimgr
