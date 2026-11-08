// SPDX-License-Identifier: MIT
#include "wifi6/transport.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "wifi6/log.hpp"

namespace wifi6 {

namespace {
constexpr const char* kTag = "udp";

sockaddr_in toSockaddr(const Endpoint& e) noexcept {
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(e.port);
    a.sin_addr.s_addr = htonl(e.ipv4);
    return a;
}
}  // namespace

bool Endpoint::parse(const char* text, Endpoint& out) noexcept {
    if (!text) return false;
    const char* colon = std::strrchr(text, ':');
    if (!colon || colon == text) return false;
    char host[INET_ADDRSTRLEN];
    const size_t hlen = static_cast<size_t>(colon - text);
    if (hlen >= sizeof host) return false;
    std::memcpy(host, text, hlen);
    host[hlen] = '\0';

    in_addr addr{};
    if (inet_pton(AF_INET, host, &addr) != 1) return false;
    char* end = nullptr;
    errno = 0;
    const long port = std::strtol(colon + 1, &end, 10);
    if (errno || end == colon + 1 || *end != '\0' || port < 1 || port > 65535) return false;

    out.ipv4 = ntohl(addr.s_addr);
    out.port = static_cast<uint16_t>(port);
    return true;
}

void Endpoint::format(char* out, size_t cap) const noexcept {
    std::snprintf(out, cap, "%u.%u.%u.%u:%u", (ipv4 >> 24) & 0xFF, (ipv4 >> 16) & 0xFF, (ipv4 >> 8) & 0xFF,
                  ipv4 & 0xFF, port);
}

Status UdpTransport::open(uint16_t localPort, const char* bindIface) noexcept {
    close();
    fd_ = ::socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd_ < 0) return Status::IoError;

    const int one = 1;
    (void)::setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);

    if (bindIface && *bindIface) {
        if (::setsockopt(fd_, SOL_SOCKET, SO_BINDTODEVICE, bindIface, static_cast<socklen_t>(std::strlen(bindIface))) <
            0) {
            W6_LOGE(kTag, "SO_BINDTODEVICE %s: %s", bindIface, std::strerror(errno));
            close();
            return Status::IoError;
        }
    }

    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(localPort);
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    if (::bind(fd_, reinterpret_cast<sockaddr*>(&a), sizeof a) < 0) {
        W6_LOGE(kTag, "bind :%u: %s", localPort, std::strerror(errno));
        close();
        return Status::IoError;
    }
    return Status::Ok;
}

void UdpTransport::close() noexcept {
    if (fd_ >= 0) ::close(fd_);
    fd_ = -1;
}

uint16_t UdpTransport::localPort() const noexcept {
    sockaddr_in a{};
    socklen_t len = sizeof a;
    if (fd_ < 0 || ::getsockname(fd_, reinterpret_cast<sockaddr*>(&a), &len) < 0) return 0;
    return ntohs(a.sin_port);
}

Status UdpTransport::setAccessCategory(AccessCategory ac) noexcept {
    if (fd_ < 0) return Status::InvalidState;
    // DSCP per RFC 8325 and the matching 802.1D user priority.
    int dscp = 0, up = 0;
    switch (ac) {
        case AccessCategory::Background: dscp = 8; up = 1; break;   // CS1   -> AC_BK
        case AccessCategory::BestEffort: dscp = 0; up = 0; break;   // CS0   -> AC_BE
        case AccessCategory::Video: dscp = 34; up = 5; break;       // AF41  -> AC_VI
        case AccessCategory::Voice: dscp = 46; up = 6; break;       // EF    -> AC_VO
    }
    const int tos = dscp << 2;
    if (::setsockopt(fd_, IPPROTO_IP, IP_TOS, &tos, sizeof tos) < 0) return Status::IoError;

    // skb->priority 256..263 makes mac80211 use the UP directly, bypassing
    // DSCP classification. Needs CAP_NET_ADMIN; DSCP alone is the fallback.
    const int prio = 256 + up;
    if (::setsockopt(fd_, SOL_SOCKET, SO_PRIORITY, &prio, sizeof prio) < 0)
        W6_LOGD(kTag, "SO_PRIORITY %d: %s (DSCP marking still applies)", prio, std::strerror(errno));
    return Status::Ok;
}

Status UdpTransport::send(const Endpoint& to, const uint8_t* data, size_t len) noexcept {
    if (fd_ < 0) return Status::InvalidState;
    const sockaddr_in a = toSockaddr(to);
    for (;;) {
        const ssize_t n = ::sendto(fd_, data, len, 0, reinterpret_cast<const sockaddr*>(&a), sizeof a);
        if (n >= 0) return static_cast<size_t>(n) == len ? Status::Ok : Status::IoError;
        switch (errno) {
            case EINTR: continue;
            case EAGAIN:
#if EAGAIN != EWOULDBLOCK
            case EWOULDBLOCK:
#endif
            case ENOBUFS: return Status::WouldBlock;
            case ENETUNREACH:
            case EHOSTUNREACH:
            case ENETDOWN: return Status::NotConnected;
            default: return Status::IoError;
        }
    }
}

Status UdpTransport::receive(uint8_t* buf, size_t cap, size_t& len, Endpoint& from) noexcept {
    len = 0;
    if (fd_ < 0) return Status::InvalidState;
    for (;;) {
        sockaddr_in a{};
        socklen_t alen = sizeof a;
        const ssize_t n =
            ::recvfrom(fd_, buf, cap, MSG_DONTWAIT | MSG_TRUNC, reinterpret_cast<sockaddr*>(&a), &alen);
        if (n < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) return Status::WouldBlock;
            if (errno == ECONNREFUSED) return Status::WouldBlock;  // ICMP from an earlier send
            return Status::IoError;
        }
        from.ipv4 = ntohl(a.sin_addr.s_addr);
        from.port = ntohs(a.sin_port);
        if (static_cast<size_t>(n) > cap) return Status::ParseError;  // datagram was truncated
        len = static_cast<size_t>(n);
        return Status::Ok;
    }
}

}  // namespace wifi6
