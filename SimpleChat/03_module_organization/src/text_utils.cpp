// text_utils.cpp
#include "text_utils.h"

#include <algorithm>
#include <cctype>
#include <ctime>

namespace simplechat {

std::string trim(const std::string& s) {
    const auto b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    const auto e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

bool startsWith(const std::string& s, const std::string& prefix) { return s.rfind(prefix, 0) == 0; }

bool contains(const std::string& s, const std::string& needle) {
    return s.find(needle) != std::string::npos;
}

std::string unquote(const std::string& s) {
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"') return s.substr(1, s.size() - 2);
    return s;
}

bool parseBool(const std::string& v) {
    const std::string l = toLower(v);
    return l == "true" || l == "1" || l == "yes" || l == "on";
}

std::string formatNow(const std::string& fmt) {
    const std::time_t now = std::time(nullptr);
    char buf[128];
    if (std::strftime(buf, sizeof buf, fmt.c_str(), std::localtime(&now)) == 0) return "?";
    return buf;
}

}  // namespace simplechat
