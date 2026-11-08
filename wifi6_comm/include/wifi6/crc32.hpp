// SPDX-License-Identifier: MIT
// CRC-32 (IEEE 802.3, reflected, poly 0xEDB88320). Table built at compile
// time so it lands in .rodata (flash) rather than RAM.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace wifi6 {
namespace detail {

constexpr std::array<uint32_t, 256> makeCrc32Table() {
    std::array<uint32_t, 256> t{};
    for (uint32_t i = 0; i < 256; ++i) {
        uint32_t c = i;
        for (int k = 0; k < 8; ++k) c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        t[i] = c;
    }
    return t;
}

inline constexpr std::array<uint32_t, 256> kCrc32Table = makeCrc32Table();

}  // namespace detail

// Pass the previous return value as `crc` to checksum data incrementally.
inline uint32_t crc32(const uint8_t* data, size_t len, uint32_t crc = 0) noexcept {
    crc = ~crc;
    for (size_t i = 0; i < len; ++i) crc = detail::kCrc32Table[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);
    return ~crc;
}

}  // namespace wifi6
