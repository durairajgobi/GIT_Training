// SPDX-License-Identifier: MIT
#include "blemgr/types.hpp"

#include <algorithm>
#include <cctype>

namespace blemgr {

const char* toString(Error e) {
    switch (e) {
    case Error::None: return "ok";
    case Error::InvalidArgument: return "invalid-argument";
    case Error::NotFound: return "not-found";
    case Error::NotReady: return "not-ready";
    case Error::NotConnected: return "not-connected";
    case Error::Busy: return "busy";
    case Error::Timeout: return "timeout";
    case Error::AuthFailed: return "auth-failed";
    case Error::NotSupported: return "not-supported";
    case Error::NotPermitted: return "not-permitted";
    case Error::Cancelled: return "cancelled";
    case Error::Failed: return "failed";
    }
    return "?";
}

std::string Status::toString() const {
    std::string s = blemgr::toString(code_);
    if (!message_.empty()) s += ": " + message_;
    return s;
}

const char* toString(AdapterState s) {
    switch (s) {
    case AdapterState::Unavailable: return "unavailable";
    case AdapterState::Off: return "off";
    case AdapterState::On: return "on";
    }
    return "?";
}

const char* toString(ConnState s) {
    switch (s) {
    case ConnState::Disconnected: return "disconnected";
    case ConnState::WaitingAdvert: return "waiting-advert";
    case ConnState::WaitingRetry: return "waiting-retry";
    case ConnState::Queued: return "queued";
    case ConnState::Connecting: return "connecting";
    case ConnState::Discovering: return "discovering";
    case ConnState::Ready: return "ready";
    case ConnState::Disconnecting: return "disconnecting";
    }
    return "?";
}

namespace {
bool allHex(std::string_view s) {
    return std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::isxdigit(c); });
}
std::string lower(std::string_view s) {
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}
int hexVal(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}
constexpr const char* kBaseSuffix = "-0000-1000-8000-00805f9b34fb";
}  // namespace

std::optional<std::string> normalizeUuid(std::string_view in) {
    while (!in.empty() && std::isspace(static_cast<unsigned char>(in.front()))) in.remove_prefix(1);
    while (!in.empty() && std::isspace(static_cast<unsigned char>(in.back()))) in.remove_suffix(1);
    if (in.size() > 2 && in[0] == '0' && (in[1] == 'x' || in[1] == 'X')) in.remove_prefix(2);

    if (in.size() == 4 && allHex(in)) return "0000" + lower(in) + kBaseSuffix;
    if (in.size() == 8 && allHex(in)) return lower(in) + kBaseSuffix;
    if (in.size() == 32 && allHex(in)) {
        std::string s = lower(in);
        return s.substr(0, 8) + "-" + s.substr(8, 4) + "-" + s.substr(12, 4) + "-" +
               s.substr(16, 4) + "-" + s.substr(20, 12);
    }
    if (in.size() == 36) {
        for (size_t i = 0; i < in.size(); ++i) {
            bool dash = (i == 8 || i == 13 || i == 18 || i == 23);
            if (dash ? in[i] != '-' : !std::isxdigit(static_cast<unsigned char>(in[i])))
                return std::nullopt;
        }
        return lower(in);
    }
    return std::nullopt;
}

std::optional<std::string> normalizeAddress(std::string_view in) {
    if (in.size() != 17) return std::nullopt;
    std::string out;
    out.reserve(17);
    for (size_t i = 0; i < 17; ++i) {
        char c = in[i];
        if (i % 3 == 2) {
            if (c != ':' && c != '-' && c != '_') return std::nullopt;
            out.push_back(':');
        } else {
            if (!std::isxdigit(static_cast<unsigned char>(c))) return std::nullopt;
            out.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
        }
    }
    return out;
}

std::optional<CharRef> CharRef::normalized() const {
    auto s = normalizeUuid(service);
    auto c = normalizeUuid(characteristic);
    if (!s || !c) return std::nullopt;
    return CharRef{*s, *c};
}

std::string CharRef::toString() const { return service + "/" + characteristic; }

bool GattCharacteristicInfo::hasFlag(std::string_view f) const {
    return std::find(flags.begin(), flags.end(), f) != flags.end();
}

bool ScanFilter::matches(const DeviceInfo& d) const {
    if (!addresses.empty() &&
        std::find(addresses.begin(), addresses.end(), d.address) == addresses.end())
        return false;
    if (minRssi && (!d.rssi || *d.rssi < *minRssi)) return false;
    if (!namePrefix.empty() && d.name.compare(0, namePrefix.size(), namePrefix) != 0)
        return false;
    if (!serviceUuids.empty()) {
        bool hit = false;
        for (const auto& u : serviceUuids) {
            if (std::find(d.serviceUuids.begin(), d.serviceUuids.end(), u) !=
                    d.serviceUuids.end() ||
                d.serviceData.count(u)) {
                hit = true;
                break;
            }
        }
        if (!hit) return false;
    }
    return true;
}

std::string ScanFilter::key() const {
    std::string k = "u=";
    for (const auto& u : serviceUuids) k += u + ",";
    k += ";r=" + (minRssi ? std::to_string(*minRssi) : std::string("-"));
    k += ";d=" + std::to_string(duplicateData);
    return k;  // host-side-only fields do not require a controller restart
}

std::vector<std::string> charPropertyFlags(uint32_t p) {
    std::vector<std::string> f;
    if (p & kPropRead) f.emplace_back("read");
    if (p & kPropWrite) f.emplace_back("write");
    if (p & kPropWriteNoResponse) f.emplace_back("write-without-response");
    if (p & kPropNotify) f.emplace_back("notify");
    if (p & kPropIndicate) f.emplace_back("indicate");
    if (p & kPropEncryptRead) f.emplace_back("encrypt-read");
    if (p & kPropEncryptWrite) f.emplace_back("encrypt-write");
    return f;
}

std::string toHex(const Bytes& b) {
    static const char* d = "0123456789abcdef";
    std::string s;
    s.reserve(b.size() * 2);
    for (uint8_t v : b) {
        s.push_back(d[v >> 4]);
        s.push_back(d[v & 0xf]);
    }
    return s;
}

std::optional<Bytes> parseHex(std::string_view s) {
    Bytes out;
    int hi = -1;
    for (char c : s) {
        if (c == ':' || c == ' ' || c == '-') {
            if (hi >= 0) return std::nullopt;  // separator inside a byte
            continue;
        }
        int v = hexVal(c);
        if (v < 0) return std::nullopt;
        if (hi < 0) {
            hi = v;
        } else {
            out.push_back(static_cast<uint8_t>(hi << 4 | v));
            hi = -1;
        }
    }
    if (hi >= 0) return std::nullopt;
    return out;
}

}  // namespace blemgr
