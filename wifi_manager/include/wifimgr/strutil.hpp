// String helpers shared by the wpa_supplicant parser, profile store and
// control protocol.
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace wifimgr {

std::string_view trim(std::string_view s);
std::vector<std::string_view> split(std::string_view s, char sep);
bool startsWith(std::string_view s, std::string_view prefix);
std::optional<long> parseInt(std::string_view s);
bool parseBool(std::string_view s, bool* out);

bool isHexString(std::string_view s);
std::string hexEncode(std::string_view bytes);
std::optional<std::string> hexDecode(std::string_view hex);

// Decodes wpa_supplicant's printf_encode() output (\\, \", \n, \r, \t, \e,
// \xNN). Used for SSIDs in SCAN_RESULTS, STATUS and events.
std::string wpaDecode(std::string_view s);

// Reversible C-style escaping for the profile store: backslash, control
// characters and bytes >= 0x7f become escape sequences so every value fits
// on one line regardless of what bytes an SSID or passphrase contains.
std::string cEscape(std::string_view s);
std::optional<std::string> cUnescape(std::string_view s);

// Control-protocol tokenizer: whitespace separated words, double quotes group
// words, backslash escapes the next character inside quotes. Returns nullopt
// on an unterminated quote.
std::optional<std::vector<std::string>> tokenize(std::string_view line);
// Inverse of tokenize() for a single argument.
std::string quoteArg(std::string_view arg);

}  // namespace wifimgr
