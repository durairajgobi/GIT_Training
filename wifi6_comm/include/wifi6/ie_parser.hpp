// SPDX-License-Identifier: MIT
// 802.11 Information Element parsing for HE (802.11ax) capabilities.
#pragma once

#include <cstddef>
#include <cstdint>

namespace wifi6 {

constexpr uint8_t kEidExtension = 255;
constexpr uint8_t kEidExtHeCapabilities = 35;
constexpr uint8_t kEidExtHeOperation = 36;

struct HeInfo {
    bool heCapable = false;
    bool twtRequester = false;
    bool twtResponder = false;
    bool heOperationPresent = false;
    uint8_t bssColor = 0;
    bool bssColorDisabled = false;
};

// Walk an IE buffer (as found in beacons / probe responses). Returns false
// if the buffer is malformed; fields found before the error are kept.
bool parseHeElements(const uint8_t* ies, size_t len, HeInfo& out) noexcept;

// Decode a hex string (no separators). Returns false on odd length,
// invalid digit or insufficient capacity.
bool hexToBytes(const char* hex, size_t hexLen, uint8_t* out, size_t cap, size_t& written) noexcept;

}  // namespace wifi6
