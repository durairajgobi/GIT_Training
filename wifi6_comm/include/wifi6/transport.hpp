// SPDX-License-Identifier: MIT
// Datagram transport abstraction and a POSIX UDP implementation with
// Wi-Fi Multimedia (WMM) access-category tagging.
#pragma once

#include <cstddef>
#include <cstdint>

#include "wifi6/status.hpp"

namespace wifi6 {

struct Endpoint {
    uint32_t ipv4 = 0;  // host byte order
    uint16_t port = 0;

    // "a.b.c.d:port"
    static bool parse(const char* text, Endpoint& out) noexcept;
    void format(char* out, size_t cap) const noexcept;  // cap >= 22

    friend bool operator==(const Endpoint& a, const Endpoint& b) noexcept {
        return a.ipv4 == b.ipv4 && a.port == b.port;
    }
};

class IDatagramTransport {
public:
    virtual ~IDatagramTransport() = default;
    virtual Status send(const Endpoint& to, const uint8_t* data, size_t len) noexcept = 0;
    // Non-blocking. Returns Status::WouldBlock when nothing is pending.
    virtual Status receive(uint8_t* buf, size_t cap, size_t& len, Endpoint& from) noexcept = 0;
    virtual int fd() const noexcept { return -1; }
};

// WMM access categories. Mapping follows RFC 8325 so that mac80211 puts the
// frames in the matching hardware EDCA queue.
enum class AccessCategory : uint8_t { Background, BestEffort, Video, Voice };

class UdpTransport final : public IDatagramTransport {
public:
    UdpTransport() noexcept = default;
    ~UdpTransport() override { close(); }
    UdpTransport(const UdpTransport&) = delete;
    UdpTransport& operator=(const UdpTransport&) = delete;

    // localPort 0 picks an ephemeral port. bindIface (e.g. "wlan0") pins the
    // socket to the Wi-Fi netdev (needs CAP_NET_RAW); nullptr = any.
    Status open(uint16_t localPort, const char* bindIface = nullptr) noexcept;
    void close() noexcept;
    Status setAccessCategory(AccessCategory ac) noexcept;
    uint16_t localPort() const noexcept;

    Status send(const Endpoint& to, const uint8_t* data, size_t len) noexcept override;
    Status receive(uint8_t* buf, size_t cap, size_t& len, Endpoint& from) noexcept override;
    int fd() const noexcept override { return fd_; }

private:
    int fd_ = -1;
};

}  // namespace wifi6
