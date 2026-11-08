// SPDX-License-Identifier: MIT
#include "wifi6/wpa_ctrl_driver.hpp"

#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "wifi6/ie_parser.hpp"
#include "wifi6/log.hpp"
#include "wifi6/wpa_parse.hpp"

namespace wifi6 {

namespace {
constexpr const char* kTag = "wpa";
std::atomic<unsigned> g_sockCounter{0};

bool startsWith(const char* s, const char* p) noexcept { return std::strncmp(s, p, std::strlen(p)) == 0; }

void toHex(const char* in, char* out, size_t cap) noexcept {
    static const char kDigits[] = "0123456789abcdef";
    size_t o = 0;
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(in); *p && o + 2 < cap; ++p) {
        out[o++] = kDigits[*p >> 4];
        out[o++] = kDigits[*p & 0x0F];
    }
    out[o] = '\0';
}
}  // namespace

WpaCtrlDriver::WpaCtrlDriver(const char* ctrlDir, const char* iface) noexcept {
    std::snprintf(ctrlPath_, sizeof ctrlPath_, "%s/%s", ctrlDir, iface);
}

WpaCtrlDriver::~WpaCtrlDriver() { close(); }

Status WpaCtrlDriver::openSocket(int& fd, char* localPath) noexcept {
    fd = ::socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return Status::IoError;

    sockaddr_un local{};
    local.sun_family = AF_UNIX;
    std::snprintf(localPath, kPathLen, "/tmp/wifi6_ctrl_%d_%u", static_cast<int>(::getpid()), g_sockCounter++);
    std::snprintf(local.sun_path, sizeof local.sun_path, "%s", localPath);
    ::unlink(localPath);
    if (::bind(fd, reinterpret_cast<sockaddr*>(&local), sizeof local) < 0) {
        W6_LOGE(kTag, "bind %s: %s", localPath, std::strerror(errno));
        closeSocket(fd, localPath);
        return Status::IoError;
    }

    sockaddr_un dest{};
    dest.sun_family = AF_UNIX;
    std::snprintf(dest.sun_path, sizeof dest.sun_path, "%s", ctrlPath_);
    if (::connect(fd, reinterpret_cast<sockaddr*>(&dest), sizeof dest) < 0) {
        W6_LOGE(kTag, "connect %s: %s (is wpa_supplicant running with ctrl_interface?)", ctrlPath_,
                std::strerror(errno));
        closeSocket(fd, localPath);
        return Status::IoError;
    }
    return Status::Ok;
}

void WpaCtrlDriver::closeSocket(int& fd, char* localPath) noexcept {
    if (fd >= 0) ::close(fd);
    fd = -1;
    if (localPath[0]) ::unlink(localPath);
    localPath[0] = '\0';
}

Status WpaCtrlDriver::request(int fd, const char* cmd, char* reply, size_t cap, size_t& len, int timeoutMs) noexcept {
    len = 0;
    if (fd < 0) return Status::NotConnected;
    if (::send(fd, cmd, std::strlen(cmd), 0) < 0) return Status::IoError;

    const uint64_t deadline = monotonicMs() + static_cast<uint64_t>(timeoutMs);
    for (;;) {
        const uint64_t now = monotonicMs();
        if (now >= deadline) return Status::Timeout;
        pollfd p{fd, POLLIN, 0};
        const int r = ::poll(&p, 1, static_cast<int>(deadline - now));
        if (r < 0) {
            if (errno == EINTR) continue;
            return Status::IoError;
        }
        if (r == 0) return Status::Timeout;
        const ssize_t n = ::recv(fd, reply, cap - 1, 0);
        if (n < 0) {
            if (errno == EINTR || errno == EAGAIN) continue;
            return Status::IoError;
        }
        reply[n] = '\0';
        if (n > 0 && reply[0] == '<') continue;  // unsolicited event, not our reply
        len = static_cast<size_t>(n);
        return Status::Ok;
    }
}

Status WpaCtrlDriver::command(const char* cmd) noexcept {
    size_t len = 0;
    const Status s = request(cmdFd_, cmd, reply_, sizeof reply_, len);
    if (!ok(s)) return s;
    if (startsWith(reply_, "OK")) return Status::Ok;
    if (startsWith(reply_, "UNKNOWN COMMAND")) return Status::NotSupported;
    if (startsWith(reply_, "FAIL-BUSY")) return Status::Busy;
    return Status::IoError;
}

Status WpaCtrlDriver::setNetwork(int id, const char* name, const char* value, bool secret) noexcept {
    char cmd[256];
    const int n = std::snprintf(cmd, sizeof cmd, "SET_NETWORK %d %s %s", id, name, value);
    Status s = (n > 0 && static_cast<size_t>(n) < sizeof cmd) ? command(cmd) : Status::InvalidArg;
    if (secret) secureZero(cmd, sizeof cmd);
    if (!ok(s)) W6_LOGW(kTag, "SET_NETWORK %s failed: %s", name, toString(s));  // never log the value
    return s;
}

Status WpaCtrlDriver::open() noexcept {
    if (cmdFd_ >= 0) return Status::Ok;
    Status s = openSocket(cmdFd_, cmdLocal_);
    if (!ok(s)) return s;
    s = openSocket(monFd_, monLocal_);
    if (!ok(s)) {
        closeSocket(cmdFd_, cmdLocal_);
        return s;
    }

    size_t len = 0;
    s = request(monFd_, "ATTACH", reply_, sizeof reply_, len);
    if (!ok(s) || !startsWith(reply_, "OK")) {
        W6_LOGE(kTag, "ATTACH failed");
        close();
        return Status::IoError;
    }
    s = request(cmdFd_, "PING", reply_, sizeof reply_, len);
    if (!ok(s) || !startsWith(reply_, "PONG")) {
        close();
        return Status::IoError;
    }
    W6_LOGI(kTag, "attached to %s", ctrlPath_);
    return Status::Ok;
}

void WpaCtrlDriver::close() noexcept {
    if (monFd_ >= 0) {
        size_t len = 0;
        (void)request(monFd_, "DETACH", reply_, sizeof reply_, len, 500);
    }
    closeSocket(monFd_, monLocal_);
    closeSocket(cmdFd_, cmdLocal_);
    networkId_ = -1;
}

Status WpaCtrlDriver::startScan() noexcept { return command("SCAN"); }

void WpaCtrlDriver::enrichFromBss(ApInfo& ap) noexcept {
    char cmd[32] = "BSS ";
    ap.bssid.format(cmd + 4, sizeof cmd - 4);
    size_t len = 0;
    if (!ok(request(cmdFd_, cmd, bssReply_, sizeof bssReply_, len)) || len == 0) return;

    // "ie=" carries the raw beacon/probe-response IEs as hex.
    const char* ie = std::strstr(bssReply_, "\nie=");
    if (!ie) return;
    ie += 4;
    size_t hexLen = 0;
    while (ie[hexLen] && ie[hexLen] != '\n') ++hexLen;

    uint8_t ies[1024];
    size_t n = 0;
    if (!hexToBytes(ie, hexLen, ies, sizeof ies, n)) return;
    HeInfo he;
    (void)parseHeElements(ies, n, he);  // keep what was parsed even if truncated
    ap.heCapable = he.heCapable;
    ap.twtResponder = he.twtResponder;
    ap.bssColor = he.bssColorDisabled ? 0 : he.bssColor;
}

Status WpaCtrlDriver::scanResults(ApInfo* out, size_t cap, size_t& count) noexcept {
    count = 0;
    size_t len = 0;
    Status s = request(cmdFd_, "SCAN_RESULTS", reply_, sizeof reply_, len);
    if (!ok(s)) return s;

    char* line = reply_;
    bool header = true;
    while (line && *line && count < cap) {
        char* nl = std::strchr(line, '\n');
        if (nl) *nl = '\0';
        if (header) {
            header = false;  // "bssid / frequency / signal level / flags / ssid"
        } else if (parseScanResultLine(line, out[count])) {
            ++count;
        }
        line = nl ? nl + 1 : nullptr;
    }
    // reply_ is no longer needed; BSS lookups use their own buffer.
    for (size_t i = 0; i < count; ++i) enrichFromBss(out[i]);
    return Status::Ok;
}

Status WpaCtrlDriver::connect(const NetworkConfig& cfg, const Bssid* pinned) noexcept {
    Status s = cfg.validate();
    if (!ok(s)) return s;

    (void)command("REMOVE_NETWORK all");
    networkId_ = -1;

    const bool sae = cfg.security == Security::Wpa3Sae || cfg.security == Security::Wpa2Wpa3;
    if (sae && !ok(command("SET sae_pwe 2"))) {
        // 6 GHz mandates SAE hash-to-element; older supplicants lack the knob.
        W6_LOGW(kTag, "sae_pwe not supported, 6 GHz SAE may fail");
    }

    size_t len = 0;
    s = request(cmdFd_, "ADD_NETWORK", reply_, sizeof reply_, len);
    if (!ok(s)) return s;
    char* end = nullptr;
    const long id = std::strtol(reply_, &end, 10);
    if (end == reply_ || id < 0) return Status::IoError;
    const int nid = static_cast<int>(id);

    // SSID as hex avoids any quoting/escaping problems.
    char hex[kMaxSsidLen * 2 + 1];
    toHex(cfg.ssid, hex, sizeof hex);

    const char* keyMgmt = "NONE";
    const char* pmf = "0";
    switch (cfg.security) {
        case Security::Open: break;
        case Security::Owe: keyMgmt = "OWE"; pmf = "2"; break;
        case Security::Wpa2Psk: keyMgmt = "WPA-PSK WPA-PSK-SHA256"; pmf = "1"; break;
        case Security::Wpa3Sae: keyMgmt = "SAE"; pmf = "2"; break;  // PMF mandatory for WPA3
        case Security::Wpa2Wpa3: keyMgmt = "WPA-PSK WPA-PSK-SHA256 SAE"; pmf = "1"; break;
        default: return Status::NotSupported;
    }

    s = setNetwork(nid, "ssid", hex);
    if (ok(s)) s = setNetwork(nid, "key_mgmt", keyMgmt);
    if (ok(s)) s = setNetwork(nid, "ieee80211w", pmf);
    if (ok(s)) s = setNetwork(nid, "scan_ssid", "1");
    if (ok(s) && cfg.needsPassphrase()) {
        char quoted[kMaxPassphraseLen + 3];
        std::snprintf(quoted, sizeof quoted, "\"%s\"", cfg.passphrase);
        s = setNetwork(nid, cfg.security == Security::Wpa3Sae ? "sae_password" : "psk", quoted, true);
        secureZero(quoted, sizeof quoted);
    }
    if (ok(s) && pinned && !pinned->isZero()) {
        char b[18];
        pinned->format(b, sizeof b);
        s = setNetwork(nid, "bssid", b);
    }
    if (ok(s)) {
        char cmd[32];
        std::snprintf(cmd, sizeof cmd, "SELECT_NETWORK %d", nid);
        s = command(cmd);
    }
    if (!ok(s)) {
        char cmd[32];
        std::snprintf(cmd, sizeof cmd, "REMOVE_NETWORK %d", nid);
        (void)command(cmd);
        return s;
    }
    networkId_ = nid;
    return Status::Ok;
}

Status WpaCtrlDriver::disconnect() noexcept { return command("DISCONNECT"); }

Status WpaCtrlDriver::roam(const Bssid& target) noexcept {
    char cmd[32] = "ROAM ";
    target.format(cmd + 5, sizeof cmd - 5);
    return command(cmd);
}

Status WpaCtrlDriver::pollEvent(DriverEvent& ev) noexcept {
    if (monFd_ < 0) return Status::NotConnected;
    char buf[1024];
    for (;;) {
        const ssize_t n = ::recv(monFd_, buf, sizeof buf - 1, MSG_DONTWAIT);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) return Status::WouldBlock;
            if (errno == EINTR) continue;
            return Status::IoError;
        }
        buf[n] = '\0';
        if (parseEvent(buf, ev)) return Status::Ok;
        W6_LOGD(kTag, "ignored: %s", buf);
    }
}

Status WpaCtrlDriver::linkStats(LinkStats& out) noexcept {
    size_t len = 0;
    const Status s = request(cmdFd_, "SIGNAL_POLL", reply_, sizeof reply_, len);
    if (!ok(s)) return s;
    if (startsWith(reply_, "FAIL")) return Status::NotConnected;
    return parseSignalPoll(reply_, out) ? Status::Ok : Status::ParseError;
}

Status WpaCtrlDriver::setupTwt(const TwtParams& p) noexcept {
    // wpa_supplicant >= 2.10; the driver must implement the TWT vendor command.
    char cmd[256];
    std::snprintf(cmd, sizeof cmd,
                  "TWT_SETUP dialog=%u exponent=%u mantissa=%u min_twt=%u setup_cmd=%u requestor=1 "
                  "trigger=%d implicit=%d flow_type=%d flow_id=%u protection=0",
                  p.dialogToken, p.exponent, p.mantissa, p.minWakeDuration, static_cast<unsigned>(p.setupCmd),
                  p.trigger ? 1 : 0, p.implicit ? 1 : 0, p.announced ? 0 : 1, p.flowId & 0x7u);
    const Status s = command(cmd);
    if (s == Status::IoError) return Status::NotSupported;  // "FAIL": driver lacks TWT support
    return s;
}

Status WpaCtrlDriver::teardownTwt(uint8_t flowId) noexcept {
    (void)flowId;  // wpa_supplicant tears down the (single) negotiated agreement
    return command("TWT_TEARDOWN");
}

}  // namespace wifi6
