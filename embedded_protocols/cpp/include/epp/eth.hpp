// eth.hpp - Ethernet helpers: constexpr MAC parsing and a frame builder.
#pragma once

#include <array>
#include <cstring>

#include "epp/core.hpp"
#include "ep/ep_eth.h"

namespace epp {
namespace eth {

class MacAddress {
public:
    constexpr MacAddress() = default;
    constexpr MacAddress(std::uint8_t a, std::uint8_t b, std::uint8_t c, std::uint8_t d, std::uint8_t e, std::uint8_t f)
        : b_{a, b, c, d, e, f}
    {
    }

    // "01:1B:19:00:00:00" (also '-' separated). Invalid input -> all zero
    // and valid() == false; usable in constant expressions.
    static constexpr MacAddress parse(const char *s)
    {
        MacAddress m;
        for (int i = 0; i < 6; i++) {
            int hi = hex(s[3 * i]), lo = hex(s[3 * i + 1]);
            char sep = s[3 * i + 2];
            if (hi < 0 || lo < 0 || (i < 5 && sep != ':' && sep != '-') || (i == 5 && sep != '\0'))
                return MacAddress();
            m.b_[i] = static_cast<std::uint8_t>(hi * 16 + lo);
        }
        m.valid_ = true;
        return m;
    }

    constexpr bool valid() const { return valid_; }
    constexpr bool multicast() const { return (b_[0] & 1) != 0; }
    constexpr bool locallyAdministered() const { return (b_[0] & 2) != 0; }
    constexpr std::uint8_t operator[](std::size_t i) const { return b_[i]; }
    const std::uint8_t *data() const { return b_.data(); }
    constexpr bool operator==(const MacAddress &o) const
    {
        for (int i = 0; i < 6; i++)
            if (b_[i] != o.b_[i])
                return false;
        return true;
    }

private:
    static constexpr int hex(char c)
    {
        return (c >= '0' && c <= '9') ? c - '0' : (c >= 'a' && c <= 'f') ? c - 'a' + 10 : (c >= 'A' && c <= 'F') ? c - 'A' + 10 : -1;
    }
    std::array<std::uint8_t, 6> b_{};
    bool valid_ = false;
};

inline constexpr MacAddress kPtpPrimary = MacAddress::parse("01:1B:19:00:00:00");
inline constexpr MacAddress kPtpPdelay = MacAddress::parse("01:80:C2:00:00:0E");

// Fluent builder into a caller-owned buffer.
class FrameBuilder {
public:
    explicit FrameBuilder(MutBytes buf) : buf_(buf) { std::memset(&h_, 0, sizeof(h_)); }
    FrameBuilder &dst(const MacAddress &m) { std::memcpy(h_.dst, m.data(), 6); return *this; }
    FrameBuilder &src(const MacAddress &m) { std::memcpy(h_.src, m.data(), 6); return *this; }
    FrameBuilder &vlan(std::uint16_t vid, std::uint8_t pcp, bool dei = false)
    {
        h_.vlan = true;
        h_.vid = vid;
        h_.pcp = pcp;
        h_.dei = dei;
        return *this;
    }
    FrameBuilder &type(std::uint16_t ethertype) { h_.ethertype = ethertype; return *this; }
    Result<std::size_t> payload(Bytes p)
    {
        std::size_t n = ep_eth_build(buf_.data(), buf_.size(), &h_, p.data(), p.size());
        if (n == 0)
            return Status::Overflow;
        return n;
    }

private:
    MutBytes buf_;
    ep_eth_hdr_t h_;
};

}  // namespace eth
}  // namespace epp
