// SPDX-License-Identifier: MIT
// wifi6-comm: Wi-Fi 6 station + telemetry/command link for embedded Linux.
//
//   * joins a WPA3/WPA2/OWE network through wpa_supplicant, preferring 6 GHz
//     and 5 GHz HE (802.11ax) access points;
//   * roams between BSSs of the same ESS and recovers from link loss;
//   * negotiates a TWT agreement to save power between telemetry bursts;
//   * streams binary telemetry over UDP (WMM-tagged) and accepts reliable,
//     ACKed commands from a back-end.
//
// --sim runs the whole stack against a simulated radio on the host, sending
// telemetry to itself over loopback.

#include <poll.h>
#include <signal.h>

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "wifi6/byte_order.hpp"
#include "wifi6/connection_manager.hpp"
#include "wifi6/log.hpp"
#include "wifi6/message_channel.hpp"
#include "wifi6/sim_driver.hpp"
#include "wifi6/transport.hpp"
#include "wifi6/wpa_ctrl_driver.hpp"

using namespace wifi6;

namespace {

constexpr const char* kTag = "app";
volatile std::sig_atomic_t g_stop = 0;

void onSignal(int) { g_stop = 1; }

struct Options {
    const char* iface = "wlan0";
    const char* ctrlDir = "/var/run/wpa_supplicant";
    const char* ssid = nullptr;
    const char* passphrase = nullptr;
    const char* peer = nullptr;
    const char* bindIface = nullptr;
    Security security = Security::Wpa3Sae;
    AccessCategory ac = AccessCategory::Video;
    uint16_t localPort = 5005;
    uint32_t telemetryMs = 1000;
    uint32_t twtIntervalMs = 0;
    uint32_t twtWakeUs = 4096;
    bool no2g4 = false;
    bool sim = false;
    bool verbose = false;
};

void usage(const char* argv0) {
    std::fprintf(stderr,
                 "Usage: %s --ssid SSID [options]\n"
                 "  --iface IF            wireless interface (default wlan0)\n"
                 "  --ctrl-dir DIR        wpa_supplicant ctrl dir (default /var/run/wpa_supplicant)\n"
                 "  --security MODE       sae | psk | transition | owe | open (default sae)\n"
                 "  --pass PASS           passphrase (prefer env WIFI6_PASSPHRASE: argv is world-readable)\n"
                 "  --peer IP:PORT        back-end endpoint for telemetry\n"
                 "  --port N              local UDP port (default 5005)\n"
                 "  --bind-iface IF       bind UDP socket to IF (SO_BINDTODEVICE)\n"
                 "  --ac bk|be|vi|vo      WMM access category (default vi)\n"
                 "  --telemetry-ms N      telemetry period (default 1000)\n"
                 "  --twt-interval-ms N   request TWT with this wake interval (0 = off)\n"
                 "  --twt-wake-us N       TWT minimum wake duration (default 4096)\n"
                 "  --no-2g4              never use 2.4 GHz\n"
                 "  --sim                 simulated radio + loopback peer (host demo)\n"
                 "  -v, --verbose         debug logging\n",
                 argv0);
}

bool parseU32(const char* s, uint32_t& out) {
    if (!s || !*s) return false;
    char* end = nullptr;
    const unsigned long v = std::strtoul(s, &end, 10);
    if (*end != '\0' || v > 0xFFFFFFFFul) return false;
    out = static_cast<uint32_t>(v);
    return true;
}

bool parseArgs(int argc, char** argv, Options& o) {
    for (int i = 1; i < argc; ++i) {
        const char* a = argv[i];
        auto next = [&]() -> const char* { return i + 1 < argc ? argv[++i] : nullptr; };
        uint32_t u = 0;
        if (!std::strcmp(a, "--iface")) {
            if (!(o.iface = next())) return false;
        } else if (!std::strcmp(a, "--ctrl-dir")) {
            if (!(o.ctrlDir = next())) return false;
        } else if (!std::strcmp(a, "--ssid")) {
            if (!(o.ssid = next())) return false;
        } else if (!std::strcmp(a, "--pass")) {
            if (!(o.passphrase = next())) return false;
        } else if (!std::strcmp(a, "--peer")) {
            if (!(o.peer = next())) return false;
        } else if (!std::strcmp(a, "--bind-iface")) {
            if (!(o.bindIface = next())) return false;
        } else if (!std::strcmp(a, "--security")) {
            const char* m = next();
            if (!m) return false;
            if (!std::strcmp(m, "sae")) o.security = Security::Wpa3Sae;
            else if (!std::strcmp(m, "psk")) o.security = Security::Wpa2Psk;
            else if (!std::strcmp(m, "transition")) o.security = Security::Wpa2Wpa3;
            else if (!std::strcmp(m, "owe")) o.security = Security::Owe;
            else if (!std::strcmp(m, "open")) o.security = Security::Open;
            else return false;
        } else if (!std::strcmp(a, "--ac")) {
            const char* m = next();
            if (!m) return false;
            if (!std::strcmp(m, "bk")) o.ac = AccessCategory::Background;
            else if (!std::strcmp(m, "be")) o.ac = AccessCategory::BestEffort;
            else if (!std::strcmp(m, "vi")) o.ac = AccessCategory::Video;
            else if (!std::strcmp(m, "vo")) o.ac = AccessCategory::Voice;
            else return false;
        } else if (!std::strcmp(a, "--port")) {
            if (!parseU32(next(), u) || u == 0 || u > 65535) return false;
            o.localPort = static_cast<uint16_t>(u);
        } else if (!std::strcmp(a, "--telemetry-ms")) {
            if (!parseU32(next(), o.telemetryMs) || o.telemetryMs < 10) return false;
        } else if (!std::strcmp(a, "--twt-interval-ms")) {
            if (!parseU32(next(), o.twtIntervalMs)) return false;
        } else if (!std::strcmp(a, "--twt-wake-us")) {
            if (!parseU32(next(), o.twtWakeUs)) return false;
        } else if (!std::strcmp(a, "--no-2g4")) {
            o.no2g4 = true;
        } else if (!std::strcmp(a, "--sim")) {
            o.sim = true;
        } else if (!std::strcmp(a, "-v") || !std::strcmp(a, "--verbose")) {
            o.verbose = true;
        } else {
            return false;
        }
    }
    return true;
}

ApInfo makeSimAp(const char* ssid, const char* bssid, uint32_t freq, int16_t rssi, Security sec, uint8_t color) {
    ApInfo ap;
    std::snprintf(ap.ssid, sizeof ap.ssid, "%s", ssid);
    Bssid::parse(bssid, ap.bssid);
    ap.freqMhz = freq;
    ap.rssiDbm = rssi;
    ap.security = sec;
    ap.heCapable = true;
    ap.twtResponder = true;
    ap.bssColor = color;
    return ap;
}

// Telemetry payload (big-endian), version 1:
//   u8 ver | u32 uptime_s | i16 rssi | i16 rssi_avg | u16 freq | u16 chan |
//   u8 band | u8 state | u8 twt | u8 mcs | u32 rate_kbps | u32 tx | u32 retx | u32 roams
constexpr size_t kTelemetryLen = 1 + 4 + 2 + 2 + 2 + 2 + 1 + 1 + 1 + 1 + 4 + 4 + 4 + 4;

class App final : public IConnectionObserver, public IMessageHandler {
public:
    App(IWifiDriver& drv, IDatagramTransport& tr, const Options& o, const ConnectionPolicy& pol,
        SimDriver* sim) noexcept
        : cm_(drv, pol, this),
          chan_(tr, *this, static_cast<uint16_t>(monotonicMs())),
          driver_(drv),
          transport_(tr),
          sim_(sim),
          telemetryMs_(o.telemetryMs) {}

    void setPeer(const Endpoint& e) { chan_.setPeer(e); }

    int run(const NetworkConfig& cfg) {
        const uint64_t t0 = monotonicMs();
        startMs_ = t0;
        Status s = cm_.start(cfg, t0);
        if (!ok(s)) {
            W6_LOGE(kTag, "invalid network configuration: %s", toString(s));
            return 2;
        }
        uint64_t nextTelemetry = t0;
        uint64_t nextPing = t0 + 3000;
        uint64_t nextFade = t0 + 8000;
        bool faded = false;

        while (!g_stop) {
            pollfd fds[2];
            nfds_t n = 0;
            if (driver_.eventFd() >= 0) fds[n++] = {driver_.eventFd(), POLLIN, 0};
            if (transport_.fd() >= 0) fds[n++] = {transport_.fd(), POLLIN, 0};
            (void)::poll(fds, n, 50);  // wake on driver/socket activity or every 50 ms

            const uint64_t now = monotonicMs();
            cm_.tick(now);
            chan_.poll(now);

            if (cm_.state() == ConnState::Failed) {
                W6_LOGE(kTag, "giving up: %s (check credentials)", toString(cm_.lastError()));
                break;
            }
            if (cm_.state() == ConnState::Connected && now >= nextTelemetry) {
                sendTelemetry(now);
                nextTelemetry = now + telemetryMs_;
            }
            if (sim_) {
                // Demo: a reliable PING over loopback, and a fading 6 GHz
                // signal that pushes the station to roam to 5 GHz and back.
                if (cm_.state() == ConnState::Connected && now >= nextPing) {
                    static const uint8_t kPing[] = {'P', 'I', 'N', 'G'};
                    (void)chan_.send(MsgType::Command, kPing, sizeof kPing, true, now);
                    nextPing = now + 5000;
                }
                if (now >= nextFade) {
                    Bssid six;
                    Bssid::parse("02:00:5e:00:06:01", six);
                    faded = !faded;
                    W6_LOGI("sim", "6 GHz AP signal -> %d dBm", faded ? -84 : -48);
                    sim_->setApRssi(six, faded ? -84 : -48);
                    nextFade = now + 40000;
                }
            }
        }
        cm_.stop();
        const ChannelStats& st = chan_.stats();
        W6_LOGI(kTag, "tx=%u rx=%u retx=%u crc=%u dup=%u fail=%u roams=%u", st.txFrames, st.rxFrames,
                st.retransmits, st.crcErrors, st.duplicates, st.deliveryFailures, cm_.roamCount());
        return 0;
    }

    // IConnectionObserver
    void onStateChanged(ConnState, ConnState to, Status) noexcept override {
        if (to == ConnState::Connected) {
            const ApInfo& ap = cm_.currentAp();
            char b[18];
            ap.bssid.format(b, sizeof b);
            W6_LOGI(kTag, "online via %s (%s ch%u, %s, BSS colour %u)", b, toString(ap.band()),
                    channelFromFreq(ap.freqMhz), toString(ap.security), ap.bssColor);
        }
    }
    void onLinkStats(const LinkStats& ls) noexcept override { last_ = ls; }
    void onRoamed(const ApInfo& ap) noexcept override {
        char b[18];
        ap.bssid.format(b, sizeof b);
        W6_LOGI(kTag, "roamed to %s (%s)", b, toString(ap.band()));
    }

    // IMessageHandler
    void onMessage(MsgType type, uint16_t seq, const uint8_t* p, size_t len, const Endpoint& from) noexcept override {
        switch (type) {
            case MsgType::Command: handleCommand(p, len, from); break;
            case MsgType::Response:
                W6_LOGI(kTag, "response #%u: %.*s", seq, static_cast<int>(len), reinterpret_cast<const char*>(p));
                break;
            case MsgType::Telemetry:
                if (len >= 7)
                    W6_LOGD(kTag, "telemetry #%u from peer: rssi %d dBm", seq,
                            static_cast<int16_t>(getBe16(p + 5)));
                break;
            default: break;
        }
    }
    void onDelivered(uint16_t seq) noexcept override { W6_LOGD(kTag, "command #%u acknowledged", seq); }
    void onDeliveryFailed(uint16_t seq) noexcept override { W6_LOGW(kTag, "command #%u not acknowledged", seq); }

private:
    void handleCommand(const uint8_t* p, size_t len, const Endpoint& from) noexcept {
        char cmd[64];
        const size_t n = len < sizeof cmd - 1 ? len : sizeof cmd - 1;
        std::memcpy(cmd, p, n);
        cmd[n] = '\0';
        char reply[128];
        uint32_t v = 0;
        if (!std::strcmp(cmd, "PING")) {
            std::snprintf(reply, sizeof reply, "PONG");
        } else if (!std::strcmp(cmd, "STATUS")) {
            const ApInfo& ap = cm_.currentAp();
            std::snprintf(reply, sizeof reply, "state=%s band=%s rssi=%d rate=%ukbps twt=%d",
                          toString(cm_.state()), toString(ap.band()), cm_.link().smoothedRssi(), last_.txRateKbps,
                          cm_.twtActive() ? 1 : 0);
        } else if (!std::strncmp(cmd, "SET_PERIOD ", 11) && parseU32(cmd + 11, v) && v >= 10 && v <= 3600000) {
            telemetryMs_ = v;
            std::snprintf(reply, sizeof reply, "OK period=%u", v);
        } else {
            std::snprintf(reply, sizeof reply, "ERR unknown command");
        }
        (void)chan_.sendTo(from, MsgType::Response, reinterpret_cast<const uint8_t*>(reply), std::strlen(reply));
    }

    void sendTelemetry(uint64_t now) noexcept {
        uint8_t b[kTelemetryLen];
        uint8_t* p = b;
        const ApInfo& ap = cm_.currentAp();
        const ChannelStats& st = chan_.stats();
        *p++ = 1;
        putBe32(p, static_cast<uint32_t>((now - startMs_) / 1000)); p += 4;
        putBe16(p, static_cast<uint16_t>(last_.rssiDbm)); p += 2;
        putBe16(p, static_cast<uint16_t>(cm_.link().smoothedRssi())); p += 2;
        putBe16(p, static_cast<uint16_t>(ap.freqMhz)); p += 2;
        putBe16(p, channelFromFreq(ap.freqMhz)); p += 2;
        *p++ = static_cast<uint8_t>(ap.band());
        *p++ = static_cast<uint8_t>(cm_.state());
        *p++ = cm_.twtActive() ? 1 : 0;
        *p++ = last_.heMcs;
        putBe32(p, last_.txRateKbps); p += 4;
        putBe32(p, st.txFrames); p += 4;
        putBe32(p, st.retransmits); p += 4;
        putBe32(p, cm_.roamCount()); p += 4;
        const Status s = chan_.send(MsgType::Telemetry, b, sizeof b, false, now);
        if (!ok(s) && s != Status::WouldBlock) W6_LOGW(kTag, "telemetry send: %s", toString(s));
    }

    ConnectionManager cm_;
    MessageChannel chan_;
    IWifiDriver& driver_;
    IDatagramTransport& transport_;
    SimDriver* sim_;
    uint32_t telemetryMs_;
    uint64_t startMs_ = 0;
    LinkStats last_{};
};

}  // namespace

int main(int argc, char** argv) {
    Options opts;
    if (!parseArgs(argc, argv, opts)) {
        usage(argv[0]);
        return 2;
    }
    if (opts.sim && !opts.ssid) opts.ssid = "FleetNet";
    if (!opts.ssid) {
        usage(argv[0]);
        return 2;
    }
    setLogLevel(opts.verbose ? LogLevel::Debug : LogLevel::Info);

    struct sigaction sa{};
    sa.sa_handler = onSignal;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
    signal(SIGPIPE, SIG_IGN);

    NetworkConfig cfg;
    cfg.security = opts.security;
    cfg.allow2g4 = !opts.no2g4;
    if (!ok(cfg.setSsid(opts.ssid))) {
        W6_LOGE(kTag, "SSID must be 1..32 bytes");
        return 2;
    }
    const char* pass = opts.passphrase ? opts.passphrase : std::getenv("WIFI6_PASSPHRASE");
    if (opts.sim && !pass) pass = "correct-horse-battery";
    if (pass && !ok(cfg.setPassphrase(pass))) {
        W6_LOGE(kTag, "passphrase too long");
        return 2;
    }

    ConnectionPolicy pol;
    if (opts.twtIntervalMs) {
        pol.enableTwt = true;
        pol.twtIntervalUs = opts.twtIntervalMs * 1000u;
        pol.twtWakeUs = opts.twtWakeUs;
    }

    SimDriver sim;
    WpaCtrlDriver wpa(opts.ctrlDir, opts.iface);
    IWifiDriver* drv = &wpa;
    if (opts.sim) {
        sim.setPassphrase(pass);
        sim.addAp(makeSimAp(opts.ssid, "02:00:5e:00:06:01", 6115, -48, Security::Wpa3Sae, 17));   // 6 GHz ch33
        sim.addAp(makeSimAp(opts.ssid, "02:00:5e:00:05:01", 5500, -58, Security::Wpa2Wpa3, 23));  // 5 GHz ch100
        sim.addAp(makeSimAp(opts.ssid, "02:00:5e:00:02:01", 2437, -45, Security::Wpa2Wpa3, 5));   // 2.4 GHz ch6
        sim.addAp(makeSimAp("Neighbour", "02:00:5e:00:99:01", 5180, -40, Security::Wpa2Psk, 9));
        if (!pol.enableTwt) {
            pol.enableTwt = true;
            pol.twtIntervalUs = 500000;
            pol.twtWakeUs = 4096;
        }
        pol.bgScanMinIntervalMs = 5000;
        drv = &sim;
    }

    UdpTransport udp;
    if (!ok(udp.open(opts.localPort, opts.sim ? nullptr : opts.bindIface))) return 1;
    if (!ok(udp.setAccessCategory(opts.ac))) W6_LOGW(kTag, "could not set WMM access category");

    char selfPeer[32];
    const char* peerText = opts.peer;
    if (!peerText && opts.sim) {
        std::snprintf(selfPeer, sizeof selfPeer, "127.0.0.1:%u", udp.localPort());
        peerText = selfPeer;
    }

    App app(*drv, udp, opts, pol, opts.sim ? &sim : nullptr);
    if (peerText) {
        Endpoint peer;
        if (!Endpoint::parse(peerText, peer)) {
            W6_LOGE(kTag, "bad --peer '%s' (expected a.b.c.d:port)", peerText);
            return 2;
        }
        app.setPeer(peer);
        W6_LOGI(kTag, "telemetry peer %s", peerText);
    } else {
        W6_LOGW(kTag, "no --peer given: link management only, no telemetry");
    }

    const int rc = app.run(cfg);
    secureZero(cfg.passphrase, sizeof cfg.passphrase);
    return rc;
}
