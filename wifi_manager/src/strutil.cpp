#include "wifimgr/strutil.hpp"

#include <cctype>
#include <cerrno>
#include <cstdlib>

namespace wifimgr {
namespace {

int hexVal(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

const char kHex[] = "0123456789abcdef";

}  // namespace

std::string_view trim(std::string_view s) {
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
  return s;
}

std::vector<std::string_view> split(std::string_view s, char sep) {
  std::vector<std::string_view> out;
  size_t start = 0;
  for (;;) {
    size_t pos = s.find(sep, start);
    if (pos == std::string_view::npos) {
      out.push_back(s.substr(start));
      return out;
    }
    out.push_back(s.substr(start, pos - start));
    start = pos + 1;
  }
}

bool startsWith(std::string_view s, std::string_view prefix) {
  return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

std::optional<long> parseInt(std::string_view s) {
  s = trim(s);
  if (s.empty() || s.size() > 20) return std::nullopt;
  std::string tmp(s);
  char* end = nullptr;
  errno = 0;
  long v = std::strtol(tmp.c_str(), &end, 10);
  if (errno != 0 || end == tmp.c_str() || *end != '\0') return std::nullopt;
  return v;
}

bool parseBool(std::string_view s, bool* out) {
  s = trim(s);
  if (s == "1" || s == "true" || s == "yes" || s == "on") {
    *out = true;
    return true;
  }
  if (s == "0" || s == "false" || s == "no" || s == "off") {
    *out = false;
    return true;
  }
  return false;
}

bool isHexString(std::string_view s) {
  if (s.empty()) return false;
  for (char c : s)
    if (hexVal(c) < 0) return false;
  return true;
}

std::string hexEncode(std::string_view bytes) {
  std::string out;
  out.reserve(bytes.size() * 2);
  for (unsigned char c : bytes) {
    out.push_back(kHex[c >> 4]);
    out.push_back(kHex[c & 0xf]);
  }
  return out;
}

std::optional<std::string> hexDecode(std::string_view hex) {
  if (hex.size() % 2 != 0) return std::nullopt;
  std::string out;
  out.reserve(hex.size() / 2);
  for (size_t i = 0; i < hex.size(); i += 2) {
    int hi = hexVal(hex[i]), lo = hexVal(hex[i + 1]);
    if (hi < 0 || lo < 0) return std::nullopt;
    out.push_back(static_cast<char>((hi << 4) | lo));
  }
  return out;
}

std::string wpaDecode(std::string_view s) {
  std::string out;
  out.reserve(s.size());
  for (size_t i = 0; i < s.size(); ++i) {
    char c = s[i];
    if (c != '\\' || i + 1 >= s.size()) {
      out.push_back(c);
      continue;
    }
    char n = s[++i];
    switch (n) {
      case '\\': out.push_back('\\'); break;
      case '"': out.push_back('"'); break;
      case 'n': out.push_back('\n'); break;
      case 'r': out.push_back('\r'); break;
      case 't': out.push_back('\t'); break;
      case 'e': out.push_back('\033'); break;
      case 'x':
        if (i + 2 < s.size() && hexVal(s[i + 1]) >= 0 && hexVal(s[i + 2]) >= 0) {
          out.push_back(static_cast<char>((hexVal(s[i + 1]) << 4) | hexVal(s[i + 2])));
          i += 2;
        } else {
          out.append("\\x");
        }
        break;
      default:
        out.push_back('\\');
        out.push_back(n);
    }
  }
  return out;
}

std::string cEscape(std::string_view s) {
  std::string out;
  out.reserve(s.size());
  for (unsigned char c : s) {
    switch (c) {
      case '\\': out.append("\\\\"); break;
      case '\n': out.append("\\n"); break;
      case '\r': out.append("\\r"); break;
      case '\t': out.append("\\t"); break;
      default:
        if (c < 0x20 || c >= 0x7f) {
          out.append("\\x");
          out.push_back(kHex[c >> 4]);
          out.push_back(kHex[c & 0xf]);
        } else {
          out.push_back(static_cast<char>(c));
        }
    }
  }
  return out;
}

std::optional<std::string> cUnescape(std::string_view s) {
  std::string out;
  out.reserve(s.size());
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] != '\\') {
      out.push_back(s[i]);
      continue;
    }
    if (++i >= s.size()) return std::nullopt;
    switch (s[i]) {
      case '\\': out.push_back('\\'); break;
      case 'n': out.push_back('\n'); break;
      case 'r': out.push_back('\r'); break;
      case 't': out.push_back('\t'); break;
      case 'x': {
        if (i + 2 >= s.size()) return std::nullopt;
        int hi = hexVal(s[i + 1]), lo = hexVal(s[i + 2]);
        if (hi < 0 || lo < 0) return std::nullopt;
        out.push_back(static_cast<char>((hi << 4) | lo));
        i += 2;
        break;
      }
      default: return std::nullopt;
    }
  }
  return out;
}

std::optional<std::vector<std::string>> tokenize(std::string_view line) {
  std::vector<std::string> out;
  size_t i = 0;
  while (i < line.size()) {
    while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i]))) ++i;
    if (i >= line.size()) break;
    std::string tok;
    bool quoted = false;
    while (i < line.size()) {
      char c = line[i];
      if (quoted) {
        if (c == '\\' && i + 1 < line.size()) {
          tok.push_back(line[i + 1]);
          i += 2;
          continue;
        }
        if (c == '"') {
          quoted = false;
          ++i;
          continue;
        }
        tok.push_back(c);
        ++i;
      } else {
        if (std::isspace(static_cast<unsigned char>(c))) break;
        if (c == '"') {
          quoted = true;
          ++i;
          continue;
        }
        tok.push_back(c);
        ++i;
      }
    }
    if (quoted) return std::nullopt;
    out.push_back(std::move(tok));
  }
  return out;
}

std::string quoteArg(std::string_view arg) {
  bool needs = arg.empty();
  for (unsigned char c : arg)
    if (std::isspace(c) || c == '"' || c == '\\') needs = true;
  if (!needs) return std::string(arg);
  std::string out = "\"";
  for (char c : arg) {
    if (c == '"' || c == '\\') out.push_back('\\');
    out.push_back(c);
  }
  out.push_back('"');
  return out;
}

}  // namespace wifimgr
