// text_utils.h - Small, pure string helpers shared by every module.
#pragma once
#include <string>

namespace simplechat {

std::string trim(const std::string& s);
std::string toLower(std::string s);
bool startsWith(const std::string& s, const std::string& prefix);
bool contains(const std::string& s, const std::string& needle);
std::string unquote(const std::string& s);
bool parseBool(const std::string& v);
std::string formatNow(const std::string& strftimeFormat);

}  // namespace simplechat
