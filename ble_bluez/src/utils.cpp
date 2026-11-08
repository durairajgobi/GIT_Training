// SPDX-License-Identifier: MIT
#include "ble/utils.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <stdexcept>

namespace ble {
namespace {

constexpr const char* kBaseUuidSuffix = "-0000-1000-8000-00805f9b34fb";

bool isHex(char c) { return std::isxdigit(static_cast<unsigned char>(c)) != 0; }

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

}  // namespace

bool isValidAddress(const std::string& a) {
    if (a.size() != 17) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (i % 3 == 2) {
            if (a[i] != ':') return false;
        } else if (!isHex(a[i])) {
            return false;
        }
    }
    return true;
}

std::string normalizeAddress(const std::string& address) {
    if (!isValidAddress(address)) throw std::invalid_argument("invalid Bluetooth address: " + address);
    std::string out = address;
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return out;
}

std::string addressToPathLeaf(const std::string& address) {
    std::string out = "dev_" + normalizeAddress(address);
    std::replace(out.begin(), out.end(), ':', '_');
    return out;
}

std::string devicePath(const std::string& adapterPath, const std::string& address) {
    return adapterPath + "/" + addressToPathLeaf(address);
}

std::string normalizeUuid(const std::string& in) {
    std::string u = lower(in);
    if (u.rfind("0x", 0) == 0) u = u.substr(2);
    auto allHex = [](const std::string& s) { return std::all_of(s.begin(), s.end(), isHex); };

    if ((u.size() == 4 || u.size() == 8) && allHex(u)) {
        return std::string(8 - u.size(), '0') + u + kBaseUuidSuffix;
    }
    if (u.size() == 36) {
        for (size_t i = 0; i < u.size(); ++i) {
            bool dash = (i == 8 || i == 13 || i == 18 || i == 23);
            if (dash ? u[i] != '-' : !isHex(u[i])) throw std::invalid_argument("invalid UUID: " + in);
        }
        return u;
    }
    throw std::invalid_argument("invalid UUID: " + in);
}

std::vector<uint8_t> parseHex(const std::string& text) {
    std::string digits;
    for (size_t i = 0; i < text.size(); ++i) {
        char c = text[i];
        if (c == '0' && i + 1 < text.size() && (text[i + 1] == 'x' || text[i + 1] == 'X') &&
            (i == 0 || !isHex(text[i - 1]))) {
            ++i;  // skip "0x" prefix
            continue;
        }
        if (c == ' ' || c == ':' || c == '-' || c == ',') continue;
        if (!isHex(c)) throw std::invalid_argument("invalid hex character in: " + text);
        digits.push_back(c);
    }
    if (digits.size() % 2 != 0) throw std::invalid_argument("odd number of hex digits: " + text);
    std::vector<uint8_t> out;
    out.reserve(digits.size() / 2);
    for (size_t i = 0; i < digits.size(); i += 2) {
        out.push_back(static_cast<uint8_t>(std::stoul(digits.substr(i, 2), nullptr, 16)));
    }
    return out;
}

std::string toHex(const std::vector<uint8_t>& data, char sep) {
    static const char* digits = "0123456789abcdef";
    std::string out;
    for (size_t i = 0; i < data.size(); ++i) {
        if (i && sep) out.push_back(sep);
        out.push_back(digits[data[i] >> 4]);
        out.push_back(digits[data[i] & 0x0f]);
    }
    return out;
}

std::string toPrintable(const std::vector<uint8_t>& data) {
    std::string out;
    for (uint8_t b : data) out.push_back(std::isprint(b) ? static_cast<char>(b) : '.');
    return out;
}

std::string uuidName(const std::string& uuid) {
    static const std::map<std::string, std::string> names = {
        {"1800", "Generic Access"},         {"1801", "Generic Attribute"},
        {"180a", "Device Information"},     {"180d", "Heart Rate"},
        {"180f", "Battery Service"},        {"1812", "Human Interface Device"},
        {"181a", "Environmental Sensing"},  {"2a00", "Device Name"},
        {"2a01", "Appearance"},             {"2a05", "Service Changed"},
        {"2a19", "Battery Level"},          {"2a24", "Model Number"},
        {"2a25", "Serial Number"},          {"2a26", "Firmware Revision"},
        {"2a27", "Hardware Revision"},      {"2a28", "Software Revision"},
        {"2a29", "Manufacturer Name"},      {"2a37", "Heart Rate Measurement"},
        {"2a6e", "Temperature"},            {"2a6f", "Humidity"},
        {"2902", "Client Characteristic Configuration"},
    };
    std::string u;
    try {
        u = normalizeUuid(uuid);
    } catch (const std::invalid_argument&) {
        return "";
    }
    if (u.compare(8, std::string::npos, kBaseUuidSuffix) != 0 || u.compare(0, 4, "0000") != 0) return "";
    auto it = names.find(u.substr(4, 4));
    return it == names.end() ? "" : it->second;
}

}  // namespace ble
