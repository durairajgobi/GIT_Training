// SPDX-License-Identifier: MIT
#include "wifi6/ie_parser.hpp"

namespace wifi6 {

bool parseHeElements(const uint8_t* ies, size_t len, HeInfo& out) noexcept {
    if (ies == nullptr && len) return false;
    size_t pos = 0;
    while (pos + 2 <= len) {
        const uint8_t id = ies[pos];
        const uint8_t elen = ies[pos + 1];
        if (pos + 2 + elen > len) return false;  // element overruns buffer
        const uint8_t* body = ies + pos + 2;

        if (id == kEidExtension && elen >= 1) {
            const uint8_t ext = body[0];
            if (ext == kEidExtHeCapabilities && elen >= 1 + 6) {
                // HE MAC Capabilities Information, first octet:
                //   B1 = TWT Requester Support, B2 = TWT Responder Support
                out.heCapable = true;
                out.twtRequester = (body[1] & 0x02) != 0;
                out.twtResponder = (body[1] & 0x04) != 0;
            } else if (ext == kEidExtHeOperation && elen >= 1 + 4) {
                // HE Operation Parameters (3 octets), then BSS Color Information:
                //   B0..B5 colour, B6 partial colour, B7 colour disabled
                const uint8_t color = body[4];
                out.heOperationPresent = true;
                out.bssColor = color & 0x3F;
                out.bssColorDisabled = (color & 0x80) != 0;
            }
        }
        pos += 2u + elen;
    }
    return pos == len;
}

bool hexToBytes(const char* hex, size_t hexLen, uint8_t* out, size_t cap, size_t& written) noexcept {
    written = 0;
    if (hexLen % 2 != 0 || hexLen / 2 > cap) return false;
    auto nib = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    for (size_t i = 0; i < hexLen; i += 2) {
        const int hi = nib(hex[i]);
        const int lo = nib(hex[i + 1]);
        if (hi < 0 || lo < 0) return false;
        out[i / 2] = static_cast<uint8_t>((hi << 4) | lo);
    }
    written = hexLen / 2;
    return true;
}

}  // namespace wifi6
