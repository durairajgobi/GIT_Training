// SPDX-License-Identifier: MIT
// Host unit tests (no framework dependency, so they also run on target).

#include <cstdio>
#include <cstring>
#include <deque>
#include <vector>

#include "wifi6/ap_selector.hpp"
#include "wifi6/backoff.hpp"
#include "wifi6/connection_manager.hpp"
#include "wifi6/crc32.hpp"
#include "wifi6/frame.hpp"
#include "wifi6/ie_parser.hpp"
#include "wifi6/link_monitor.hpp"
#include "wifi6/log.hpp"
#include "wifi6/message_channel.hpp"
#include "wifi6/ring_buffer.hpp"
#include "wifi6/sim_driver.hpp"
#include "wifi6/transport.hpp"
#include "wifi6/twt.hpp"
#include "wifi6/wpa_parse.hpp"

using namespace wifi6;

// ---------------------------------------------------------------- harness
namespace {
int g_checks = 0;
int g_failures = 0;

struct TestCase {
    const char* name;
    void (*fn)();
};
TestCase g_tests[64];
int g_testCount = 0;

struct Registrar {
    Registrar(const char* n, void (*f)()) { g_tests[g_testCount++] = {n, f}; }
};
}  // namespace

#define CHECK(c)                                                                     \
    do {                                                                             \
        ++g_checks;                                                                  \
        if (!(c)) {                                                                  \
            ++g_failures;                                                            \
            std::fprintf(stderr, "  %s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #c); \
        }                                                                            \
    } while (0)
#define CHECK_EQ(a, b) CHECK((a) == (b))
#define TEST(name)                                   \
    static void name();                              \
    static Registrar reg_##name(#name, &name);       \
    static void name()

// ---------------------------------------------------------------- helpers
namespace {
Bssid mac(const char* s) {
    Bssid b;
    Bssid::parse(s, b);
    return b;
}

ApInfo makeAp(const char* ssid, const char* bssid, uint32_t freq, int16_t rssi, Security sec, bool he = true,
              bool twt = true) {
    ApInfo ap;
    std::snprintf(ap.ssid, sizeof ap.ssid, "%s", ssid);
    ap.bssid = mac(bssid);
    ap.freqMhz = freq;
    ap.rssiDbm = rssi;
    ap.security = sec;
    ap.heCapable = he;
    ap.twtResponder = twt;
    return ap;
}

NetworkConfig makeCfg(const char* ssid, const char* pass, Security sec) {
    NetworkConfig c;
    c.setSsid(ssid);
    c.setPassphrase(pass);
    c.security = sec;
    return c;
}

struct RecordingObserver : IConnectionObserver {
    int transitions = 0;
    int roams = 0;
    int statsCount = 0;
    ConnState lastTo = ConnState::Idle;
    void onStateChanged(ConnState, ConnState to, Status) noexcept override {
        ++transitions;
        lastTo = to;
    }
    void onLinkStats(const LinkStats&) noexcept override { ++statsCount; }
    void onRoamed(const ApInfo&) noexcept override { ++roams; }
};

// In-memory datagram pipe for MessageChannel tests.
struct MemTransport : IDatagramTransport {
    struct Dgram {
        std::vector<uint8_t> data;
        Endpoint from;
    };
    MemTransport* peer = nullptr;
    Endpoint self{};
    int dropCount = 0;
    bool corruptNext = false;
    std::deque<Dgram> inbox;

    Status send(const Endpoint&, const uint8_t* d, size_t n) noexcept override {
        if (dropCount > 0) {
            --dropCount;
            return Status::Ok;
        }
        Dgram g{std::vector<uint8_t>(d, d + n), self};
        if (corruptNext) {
            g.data[g.data.size() / 2] ^= 0x40;
            corruptNext = false;
        }
        peer->inbox.push_back(std::move(g));
        return Status::Ok;
    }
    Status receive(uint8_t* buf, size_t cap, size_t& len, Endpoint& from) noexcept override {
        if (inbox.empty()) return Status::WouldBlock;
        Dgram g = std::move(inbox.front());
        inbox.pop_front();
        if (g.data.size() > cap) return Status::ParseError;
        std::memcpy(buf, g.data.data(), g.data.size());
        len = g.data.size();
        from = g.from;
        return Status::Ok;
    }
};

struct RecordingHandler : IMessageHandler {
    int messages = 0;
    int delivered = 0;
    int failed = 0;
    MsgType lastType = MsgType::Heartbeat;
    std::vector<uint8_t> lastPayload;
    void onMessage(MsgType t, uint16_t, const uint8_t* p, size_t n, const Endpoint&) noexcept override {
        ++messages;
        lastType = t;
        lastPayload.assign(p, p + n);
    }
    void onDelivered(uint16_t) noexcept override { ++delivered; }
    void onDeliveryFailed(uint16_t) noexcept override { ++failed; }
};
}  // namespace

// ---------------------------------------------------------------- primitives
TEST(crc32_check_value) {
    const char* s = "123456789";
    CHECK_EQ(crc32(reinterpret_cast<const uint8_t*>(s), 9), 0xCBF43926u);
    // incremental == one-shot
    uint32_t c = crc32(reinterpret_cast<const uint8_t*>(s), 4);
    c = crc32(reinterpret_cast<const uint8_t*>(s + 4), 5, c);
    CHECK_EQ(c, 0xCBF43926u);
}

TEST(ring_buffer_wraps_and_reports_full) {
    SpscRing<int, 4> r;
    CHECK(r.empty());
    for (int i = 0; i < 4; ++i) CHECK(r.push(i));
    CHECK(!r.push(99));
    int v = -1;
    CHECK(r.pop(v) && v == 0);
    CHECK(r.push(4));
    for (int expect = 1; expect <= 4; ++expect) CHECK(r.pop(v) && v == expect);
    CHECK(!r.pop(v));
}

TEST(backoff_grows_with_jitter_and_caps) {
    Backoff b(1000, 8000, 42);
    const uint32_t lo[] = {500, 1000, 2000, 4000, 4000, 4000};
    const uint32_t hi[] = {1000, 2000, 4000, 8000, 8000, 8000};
    for (int i = 0; i < 6; ++i) {
        const uint32_t d = b.next();
        CHECK(d >= lo[i] && d <= hi[i]);
    }
    b.reset();
    CHECK(b.next() <= 1000);
}

TEST(band_and_channel_mapping) {
    CHECK(bandFromFreq(2412) == Band::Ghz2_4);
    CHECK_EQ(channelFromFreq(2412), 1);
    CHECK_EQ(channelFromFreq(2484), 14);
    CHECK(bandFromFreq(5180) == Band::Ghz5);
    CHECK_EQ(channelFromFreq(5180), 36);
    CHECK_EQ(channelFromFreq(5825), 165);
    CHECK(bandFromFreq(5955) == Band::Ghz6);
    CHECK_EQ(channelFromFreq(5955), 1);
    CHECK_EQ(channelFromFreq(6115), 33);
    CHECK_EQ(channelFromFreq(7115), 233);
    CHECK_EQ(channelFromFreq(5935), 2);
    CHECK(bandFromFreq(60000) == Band::Unknown);
}

TEST(bssid_parse_and_format) {
    Bssid b;
    CHECK(Bssid::parse("02:1A:bc:00:ff:09", b));
    char s[18];
    b.format(s, sizeof s);
    CHECK(std::strcmp(s, "02:1a:bc:00:ff:09") == 0);
    CHECK(!Bssid::parse("02:1a:bc:00:ff", b));
    CHECK(!Bssid::parse("02-1a-bc-00-ff-09", b));
    CHECK(!Bssid::parse("02:1a:bc:00:ff:0g", b));
    CHECK(!Bssid::parse("02:1a:bc:00:ff:091", b));
}

TEST(network_config_validation) {
    NetworkConfig c = makeCfg("Fleet", "short", Security::Wpa2Psk);
    CHECK(c.validate() == Status::InvalidArg);  // WPA2 needs >= 8 chars
    c.security = Security::Wpa3Sae;
    CHECK(ok(c.validate()));                    // SAE has no minimum
    c.security = Security::Owe;
    CHECK(ok(c.validate()));
    c.security = Security::Enterprise;
    CHECK(c.validate() == Status::NotSupported);
    CHECK(c.setSsid("") == Status::InvalidArg);
    CHECK(c.setSsid("123456789012345678901234567890123") == Status::InvalidArg);  // 33 bytes
    NetworkConfig ctrl = makeCfg("Fleet", "bad\npassword", Security::Wpa2Psk);
    CHECK(ctrl.validate() == Status::InvalidArg);
}

// ---------------------------------------------------------------- framing
TEST(frame_roundtrip) {
    const uint8_t payload[] = {1, 2, 3, 4, 5};
    uint8_t buf[kMaxFrameSize];
    size_t n = 0;
    CHECK(ok(encodeFrame(MsgType::Telemetry, kFlagAckRequired, 0xBEEF, payload, sizeof payload, buf, sizeof buf, n)));
    CHECK_EQ(n, kFrameHeaderSize + sizeof payload + kFrameCrcSize);
    FrameView f;
    CHECK(ok(decodeFrame(buf, n, f)));
    CHECK(f.type == MsgType::Telemetry);
    CHECK_EQ(f.seq, 0xBEEF);
    CHECK(f.ackRequired());
    CHECK_EQ(f.payloadLen, sizeof payload);
    CHECK(std::memcmp(f.payload, payload, sizeof payload) == 0);
}

TEST(frame_rejects_corruption_and_bad_sizes) {
    const uint8_t payload[] = {9, 8, 7};
    uint8_t buf[64];
    size_t n = 0;
    CHECK(ok(encodeFrame(MsgType::Command, 0, 1, payload, sizeof payload, buf, sizeof buf, n)));
    FrameView f;
    buf[11] ^= 0x01;
    CHECK(decodeFrame(buf, n, f) == Status::CrcError);
    buf[11] ^= 0x01;
    CHECK(decodeFrame(buf, n - 1, f) == Status::ParseError);  // truncated
    buf[0] = 0;
    CHECK(decodeFrame(buf, n, f) == Status::ParseError);      // bad magic

    static uint8_t big[kMaxPayload + 1];
    static uint8_t out[kMaxFrameSize + 16];
    CHECK(encodeFrame(MsgType::Command, 0, 1, big, sizeof big, out, sizeof out, n) == Status::InvalidArg);
    CHECK(encodeFrame(MsgType::Command, 0, 1, payload, sizeof payload, out, 10, n) == Status::BufferFull);
    CHECK(ok(encodeFrame(MsgType::Ack, 0, 7, nullptr, 0, out, sizeof out, n)));
    CHECK(ok(decodeFrame(out, n, f)) && f.payloadLen == 0 && f.type == MsgType::Ack);
}

// ---------------------------------------------------------------- 802.11ax
TEST(twt_encodes_mantissa_exponent) {
    TwtParams p;
    CHECK(ok(TwtParams::fromMicros(1000000, 8000, p)));
    CHECK_EQ(p.exponent, 4);
    CHECK_EQ(p.mantissa, 62500);
    CHECK_EQ(p.intervalUs(), 1000000u);
    CHECK_EQ(p.minWakeDuration, 32);  // 8000 / 256 = 31.25 -> 32
    CHECK(ok(TwtParams::fromMicros(50000, 1, p)));
    CHECK_EQ(p.exponent, 0);
    CHECK_EQ(p.mantissa, 50000);
    CHECK_EQ(p.minWakeDuration, 1);
    CHECK(ok(TwtParams::fromMicros(3600ull * 1000000ull, 4096, p)));  // one hour
    CHECK(p.intervalUs() > 3599000000ull && p.intervalUs() < 3601000000ull);
    CHECK(TwtParams::fromMicros(1000, 2000, p) == Status::InvalidArg);    // wake >= interval
    CHECK(TwtParams::fromMicros(1000000, 70000, p) == Status::InvalidArg); // > 255 units
    CHECK(TwtParams::fromMicros(0, 100, p) == Status::InvalidArg);
}

TEST(he_information_elements) {
    const uint8_t ies[] = {
        0, 4, 'T', 'e', 's', 't',                              // SSID
        255, 22, 35, 0x04, 0, 0, 0, 0, 0,                      // HE Capabilities: TWT responder
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        255, 7, 36, 0x00, 0x00, 0x00, 0x2A, 0xFC, 0xFF,       // HE Operation: BSS colour 42
    };
    HeInfo he;
    CHECK(parseHeElements(ies, sizeof ies, he));
    CHECK(he.heCapable);
    CHECK(he.twtResponder);
    CHECK(!he.twtRequester);
    CHECK(he.heOperationPresent);
    CHECK_EQ(he.bssColor, 42);
    CHECK(!he.bssColorDisabled);

    HeInfo bad;
    const uint8_t truncated[] = {0, 10, 'x'};
    CHECK(!parseHeElements(truncated, sizeof truncated, bad));

    uint8_t out[4];
    size_t n = 0;
    CHECK(hexToBytes("dd0aFF", 6, out, sizeof out, n) && n == 3 && out[0] == 0xDD && out[2] == 0xFF);
    CHECK(!hexToBytes("abc", 3, out, sizeof out, n));
    CHECK(!hexToBytes("zz", 2, out, sizeof out, n));
}

// ---------------------------------------------------------------- wpa_supplicant protocol
TEST(wpa_scan_result_parsing) {
    ApInfo ap;
    CHECK(parseScanResultLine("02:00:00:00:01:00\t6115\t-52\t[RSN-SAE-CCMP][ESS]\tFleetNet", ap));
    CHECK(std::strcmp(ap.ssid, "FleetNet") == 0);
    CHECK_EQ(ap.freqMhz, 6115u);
    CHECK_EQ(ap.rssiDbm, -52);
    CHECK(ap.security == Security::Wpa3Sae);
    CHECK(ap.band() == Band::Ghz6);

    CHECK(parseScanResultLine("02:00:00:00:02:00\t5180\t-60\t[WPA2-PSK+SAE-CCMP][ESS]\t", ap));
    CHECK(ap.security == Security::Wpa2Wpa3);
    CHECK(ap.ssid[0] == '\0');  // hidden SSID
    CHECK(!parseScanResultLine("garbage", ap));
    CHECK(!parseScanResultLine("02:00:00:00:02:00\tabc\t-60\t[ESS]\tx", ap));

    CHECK(securityFromFlags("[WPA2-PSK-CCMP][ESS]") == Security::Wpa2Psk);
    CHECK(securityFromFlags("[RSN-OWE-CCMP][ESS]") == Security::Owe);
    CHECK(securityFromFlags("[WPA2-EAP-CCMP][ESS]") == Security::Enterprise);
    CHECK(securityFromFlags("[WEP][ESS]") == Security::Unsupported);
    CHECK(securityFromFlags("[ESS]") == Security::Open);
}

TEST(wpa_event_parsing) {
    DriverEvent ev;
    CHECK(parseEvent("<3>CTRL-EVENT-SCAN-RESULTS ", ev) && ev.type == DriverEventType::ScanDone);
    CHECK(parseEvent("<3>CTRL-EVENT-CONNECTED - Connection to 02:00:00:00:01:00 completed [id=0 id_str=]", ev));
    CHECK(ev.type == DriverEventType::Connected && ev.bssid == mac("02:00:00:00:01:00"));
    CHECK(parseEvent("<3>CTRL-EVENT-DISCONNECTED bssid=02:00:00:00:01:00 reason=4 locally_generated=1", ev));
    CHECK(ev.type == DriverEventType::Disconnected && ev.reason == 4);
    CHECK(parseEvent("<3>CTRL-EVENT-SSID-TEMP-DISABLED id=0 ssid=\"Fleet\" auth_failures=1 duration=10 "
                     "reason=WRONG_KEY",
                     ev));
    CHECK(ev.type == DriverEventType::AuthFailed);
    CHECK(parseEvent("<3>CTRL-EVENT-SSID-TEMP-DISABLED id=0 ssid=\"Fleet\" reason=CONN_FAILED", ev));
    CHECK(ev.type == DriverEventType::ConnectFailed);
    CHECK(parseEvent("<3>CTRL-EVENT-SIGNAL-CHANGE above=0 signal=-78 noise=-95 txrate=86000", ev));
    CHECK(ev.type == DriverEventType::SignalChange && ev.rssiDbm == -78);
    CHECK(parseEvent("<3>CTRL-EVENT-NETWORK-NOT-FOUND", ev) && ev.type == DriverEventType::NetworkNotFound);
    CHECK(parseEvent("<2>CTRL-EVENT-TERMINATING", ev) && ev.type == DriverEventType::DriverTerminated);
    CHECK(!parseEvent("<3>WPS-AP-AVAILABLE", ev));

    LinkStats ls;
    CHECK(parseSignalPoll("RSSI=-61\nLINKSPEED=1201\nNOISE=9999\nFREQUENCY=5500\nWIDTH=80 MHz\n", ls));
    CHECK_EQ(ls.rssiDbm, -61);
    CHECK_EQ(ls.txRateKbps, 1201000u);
    CHECK_EQ(ls.noiseDbm, kNoiseUnknown);
    CHECK_EQ(ls.freqMhz, 5500u);
    CHECK(!parseSignalPoll("FAIL\n", ls));

    char v[16];
    CHECK(findValue("AVG_RSSI=-70\nRSSI=-65", "RSSI", v, sizeof v) && std::strcmp(v, "-65") == 0);
}

// ---------------------------------------------------------------- selection / link
TEST(selector_prefers_strong_6ghz_but_falls_back_when_weak) {
    ApSelector sel;
    NetworkConfig cfg = makeCfg("Fleet", "secret-pass", Security::Wpa3Sae);
    ApInfo aps[] = {
        makeAp("Fleet", "02:00:00:00:00:05", 5180, -50, Security::Wpa2Wpa3),
        makeAp("Fleet", "02:00:00:00:00:06", 6115, -53, Security::Wpa3Sae),
        makeAp("Other", "02:00:00:00:00:07", 5200, -30, Security::Wpa3Sae),
        makeAp("Fleet", "02:00:00:00:00:08", 5240, -35, Security::Wpa2Psk),  // incompatible security
    };
    const ApInfo* best = sel.selectBest(aps, 4, cfg);
    CHECK(best && best->bssid == aps[1].bssid);  // 6 GHz: -53+20+4 > 5 GHz: -50+15+4

    aps[1].rssiDbm = -74;  // 6 GHz loses its bonus below -72 dBm
    best = sel.selectBest(aps, 4, cfg);
    CHECK(best && best->bssid == aps[0].bssid);

    cfg.allow5g = false;
    best = sel.selectBest(aps, 4, cfg);
    CHECK(best && best->bssid == aps[1].bssid);

    // WPA2-only configuration must never pick a 6 GHz BSS.
    NetworkConfig wpa2 = makeCfg("Fleet", "secret-pass", Security::Wpa2Psk);
    ApInfo only6[] = {makeAp("Fleet", "02:00:00:00:00:06", 6115, -40, Security::Wpa2Wpa3)};
    CHECK(sel.selectBest(only6, 1, wpa2) == nullptr);
}

TEST(selector_roaming_hysteresis) {
    ApSelector sel;
    NetworkConfig cfg = makeCfg("Fleet", "secret-pass", Security::Wpa3Sae);
    ApInfo aps[] = {
        makeAp("Fleet", "02:00:00:00:00:01", 5180, -66, Security::Wpa3Sae),
        makeAp("Fleet", "02:00:00:00:00:02", 5500, -66, Security::Wpa3Sae),
    };
    // Strong enough: never roam.
    CHECK(sel.selectRoamTarget(aps, 2, cfg, aps[0].bssid, -66) == nullptr);
    // Weak, but candidate only 5 dB better (< 8 dB hysteresis): stay.
    CHECK(sel.selectRoamTarget(aps, 2, cfg, aps[0].bssid, -71) == nullptr);
    // Clearly better candidate: roam.
    aps[1].rssiDbm = -55;
    const ApInfo* t = sel.selectRoamTarget(aps, 2, cfg, aps[0].bssid, -71);
    CHECK(t && t->bssid == aps[1].bssid);
}

TEST(link_monitor_smoothing_and_hysteresis) {
    LinkMonitor m(-72, 4);
    LinkStats s;
    s.rssiDbm = -60;
    m.update(s);
    CHECK_EQ(m.smoothedRssi(), -60);
    s.rssiDbm = -90;
    m.update(s);  // one bad sample must not flip state
    CHECK(!m.isWeak());
    for (int i = 0; i < 20; ++i) m.update(s);
    CHECK(m.isWeak());
    s.rssiDbm = -70;  // above threshold but inside hysteresis band
    for (int i = 0; i < 40; ++i) m.update(s);
    CHECK(m.isWeak());
    s.rssiDbm = -60;
    for (int i = 0; i < 40; ++i) m.update(s);
    CHECK(!m.isWeak());
}

// ---------------------------------------------------------------- connection manager
namespace {
struct Fixture {
    SimDriver sim;
    RecordingObserver obs;
    ConnectionPolicy pol;
    Bssid ap5 = mac("02:00:00:00:00:05");
    Bssid ap6 = mac("02:00:00:00:00:06");

    Fixture() {
        sim.setPassphrase("correct horse");
        sim.addAp(makeAp("Fleet", "02:00:00:00:00:05", 5180, -50, Security::Wpa2Wpa3));
        sim.addAp(makeAp("Fleet", "02:00:00:00:00:06", 6115, -52, Security::Wpa3Sae));
        sim.addAp(makeAp("Other", "02:00:00:00:00:99", 2412, -30, Security::Open));
        pol.enableTwt = true;
        pol.twtIntervalUs = 1000000;
        pol.twtWakeUs = 8000;
    }
};
}  // namespace

TEST(conn_joins_best_ap_and_negotiates_twt) {
    Fixture f;
    ConnectionManager cm(f.sim, f.pol, &f.obs);
    CHECK(ok(cm.start(makeCfg("Fleet", "correct horse", Security::Wpa3Sae), 0)));
    CHECK(cm.state() == ConnState::Scanning);
    cm.tick(10);
    CHECK(cm.state() == ConnState::Connected);
    CHECK(f.sim.connectedBssid() == f.ap6);
    CHECK(cm.currentAp().band() == Band::Ghz6);
    CHECK(cm.twtActive());
    CHECK(f.sim.twtActive());
    CHECK_EQ(f.sim.lastTwt().intervalUs(), 1000000u);
    cm.tick(20);
    CHECK(f.obs.statsCount >= 1);
    cm.stop();
    CHECK(cm.state() == ConnState::Idle);
    CHECK(!f.sim.isConnected());
}

TEST(conn_skips_twt_when_unsupported) {
    Fixture f;
    f.sim.setTwtSupported(false);
    ConnectionManager cm(f.sim, f.pol, &f.obs);
    cm.start(makeCfg("Fleet", "correct horse", Security::Wpa3Sae), 0);
    cm.tick(10);
    CHECK(cm.state() == ConnState::Connected);
    CHECK(!cm.twtActive());
}

TEST(conn_roams_when_signal_fades) {
    Fixture f;
    ConnectionManager cm(f.sim, f.pol, &f.obs);
    cm.start(makeCfg("Fleet", "correct horse", Security::Wpa3Sae), 0);
    cm.tick(10);
    CHECK(f.sim.connectedBssid() == f.ap6);

    f.sim.setApRssi(f.ap6, -82);  // walk away from the 6 GHz AP
    for (uint64_t t = 1000; t <= 60000 && f.sim.roamCount() == 0; t += 1000) cm.tick(t);
    cm.tick(61000);
    CHECK_EQ(f.sim.roamCount(), 1u);
    CHECK(f.sim.connectedBssid() == f.ap5);
    CHECK(cm.currentAp().bssid == f.ap5);
    CHECK_EQ(f.obs.roams, 1);
    CHECK_EQ(cm.roamCount(), 1u);
    CHECK(cm.state() == ConnState::Connected);
    CHECK(f.sim.twtActive());  // re-negotiated after reassociation
}

TEST(conn_recovers_after_link_loss) {
    Fixture f;
    ConnectionManager cm(f.sim, f.pol, &f.obs);
    cm.start(makeCfg("Fleet", "correct horse", Security::Wpa3Sae), 0);
    cm.tick(10);
    const uint32_t attempts = f.sim.connectAttempts();
    f.sim.dropLink();
    cm.tick(100);
    CHECK(cm.state() == ConnState::Backoff);
    CHECK(cm.lastError() == Status::NotConnected);
    CHECK(!cm.twtActive());
    cm.tick(1200);  // first backoff is at most 1 s
    cm.tick(1210);
    CHECK(cm.state() == ConnState::Connected);
    CHECK_EQ(f.sim.connectAttempts(), attempts + 1);
}

TEST(conn_stops_after_repeated_auth_failures) {
    Fixture f;
    ConnectionManager cm(f.sim, f.pol, &f.obs);
    cm.start(makeCfg("Fleet", "wrong password", Security::Wpa3Sae), 0);
    for (uint64_t t = 10; t < 30000 && cm.state() != ConnState::Failed; t += 50) cm.tick(t);
    CHECK(cm.state() == ConnState::Failed);
    CHECK(cm.lastError() == Status::AuthFailed);
    CHECK_EQ(f.sim.connectAttempts(), 3u);
    const uint32_t scans = f.sim.scanCount();
    cm.tick(100000);
    CHECK_EQ(f.sim.scanCount(), scans);  // no more retries
}

TEST(conn_backs_off_when_network_absent) {
    Fixture f;
    ConnectionManager cm(f.sim, f.pol, &f.obs);
    cm.start(makeCfg("Missing", "correct horse", Security::Wpa3Sae), 0);
    cm.tick(10);
    CHECK(cm.state() == ConnState::Backoff);
    CHECK(cm.lastError() == Status::NoNetwork);
    CHECK_EQ(f.sim.connectAttempts(), 0u);
}

TEST(conn_reopens_driver_after_termination) {
    Fixture f;
    ConnectionManager cm(f.sim, f.pol, &f.obs);
    cm.start(makeCfg("Fleet", "correct horse", Security::Wpa3Sae), 0);
    cm.tick(10);
    f.sim.terminate();
    cm.tick(20);
    CHECK(cm.state() == ConnState::Backoff);
    cm.tick(2000);
    cm.tick(2010);
    CHECK(cm.state() == ConnState::Connected);
}

TEST(conn_rejects_invalid_config) {
    Fixture f;
    ConnectionManager cm(f.sim, f.pol);
    CHECK(cm.start(makeCfg("Fleet", "short", Security::Wpa2Psk), 0) == Status::InvalidArg);
    CHECK(cm.state() == ConnState::Idle);
}

// ---------------------------------------------------------------- messaging
namespace {
struct ChannelPair {
    MemTransport ta, tb;
    RecordingHandler ha, hb;
    MessageChannel a{ta, ha, 100};
    MessageChannel b{tb, hb, 500};
    ChannelPair() {
        ta.peer = &tb;
        tb.peer = &ta;
        Endpoint::parse("10.0.0.1:5005", ta.self);
        Endpoint::parse("10.0.0.2:5005", tb.self);
        a.setPeer(tb.self);
        b.setPeer(ta.self);
    }
};
}  // namespace

TEST(channel_reliable_delivery) {
    ChannelPair p;
    const uint8_t cmd[] = {'P', 'I', 'N', 'G'};
    uint16_t seq = 0;
    CHECK(ok(p.a.send(MsgType::Command, cmd, sizeof cmd, true, 0, &seq)));
    CHECK_EQ(seq, 100);
    CHECK_EQ(p.a.pendingCount(), 1u);
    p.b.poll(1);
    CHECK_EQ(p.hb.messages, 1);
    CHECK(p.hb.lastType == MsgType::Command);
    CHECK(p.hb.lastPayload.size() == 4 && p.hb.lastPayload[0] == 'P');
    p.a.poll(2);
    CHECK_EQ(p.ha.delivered, 1);
    CHECK_EQ(p.a.pendingCount(), 0u);
}

TEST(channel_retransmits_lost_frame) {
    ChannelPair p;
    const uint8_t cmd[] = {1};
    p.ta.dropCount = 1;
    CHECK(ok(p.a.send(MsgType::Command, cmd, 1, true, 0)));
    p.b.poll(10);
    CHECK_EQ(p.hb.messages, 0);
    p.a.poll(300);  // past 250 ms ack timeout
    CHECK_EQ(p.a.stats().retransmits, 1u);
    p.b.poll(310);
    CHECK_EQ(p.hb.messages, 1);
    p.a.poll(320);
    CHECK_EQ(p.ha.delivered, 1);
}

TEST(channel_suppresses_duplicates_when_ack_lost) {
    ChannelPair p;
    const uint8_t cmd[] = {1};
    p.a.send(MsgType::Command, cmd, 1, true, 0);
    p.tb.dropCount = 1;  // B's ACK is lost
    p.b.poll(10);
    p.a.poll(300);       // A retransmits
    p.b.poll(310);       // B sees a duplicate, re-ACKs, does not re-deliver
    p.a.poll(320);
    CHECK_EQ(p.hb.messages, 1);
    CHECK_EQ(p.b.stats().duplicates, 1u);
    CHECK_EQ(p.ha.delivered, 1);
}

TEST(channel_reports_delivery_failure) {
    ChannelPair p;
    const uint8_t cmd[] = {1};
    p.ta.dropCount = 1000;
    p.a.send(MsgType::Command, cmd, 1, true, 0);
    for (uint64_t t = 0; t <= 5000; t += 50) p.a.poll(t);
    CHECK_EQ(p.ha.failed, 1);
    CHECK_EQ(p.a.stats().retransmits, 3u);
    CHECK_EQ(p.a.pendingCount(), 0u);
}

TEST(channel_counts_corrupted_frames) {
    ChannelPair p;
    const uint8_t t[] = {1, 2, 3, 4, 5, 6, 7, 8};
    p.ta.corruptNext = true;
    CHECK(ok(p.a.send(MsgType::Telemetry, t, sizeof t, false, 0)));
    p.b.poll(1);
    CHECK_EQ(p.hb.messages, 0);
    CHECK_EQ(p.b.stats().crcErrors, 1u);
}

TEST(channel_pending_window_is_bounded) {
    ChannelPair p;
    const uint8_t cmd[] = {1};
    for (size_t i = 0; i < MessageChannel::kMaxPending; ++i) CHECK(ok(p.a.send(MsgType::Command, cmd, 1, true, 0)));
    CHECK(p.a.send(MsgType::Command, cmd, 1, true, 0) == Status::BufferFull);
}

TEST(udp_loopback_roundtrip) {
    UdpTransport u;
    if (!ok(u.open(0))) {
        std::fprintf(stderr, "  (skipped: cannot open UDP socket)\n");
        return;
    }
    CHECK(ok(u.setAccessCategory(AccessCategory::Voice)));
    Endpoint self;
    char addr[32];
    std::snprintf(addr, sizeof addr, "127.0.0.1:%u", u.localPort());
    CHECK(Endpoint::parse(addr, self));
    const uint8_t msg[] = {0xCA, 0xFE};
    CHECK(ok(u.send(self, msg, sizeof msg)));
    uint8_t buf[16];
    size_t n = 0;
    Endpoint from;
    Status s = Status::WouldBlock;
    for (int i = 0; i < 100 && s == Status::WouldBlock; ++i) s = u.receive(buf, sizeof buf, n, from);
    CHECK(ok(s));
    CHECK(n == 2 && buf[0] == 0xCA && buf[1] == 0xFE);
    CHECK(from == self);

    Endpoint e;
    CHECK(!Endpoint::parse("300.1.1.1:5", e));
    CHECK(!Endpoint::parse("10.0.0.1:0", e));
    CHECK(!Endpoint::parse("10.0.0.1", e));
}

// ---------------------------------------------------------------- main
int main() {
    setLogLevel(LogLevel::Error);
    for (int i = 0; i < g_testCount; ++i) {
        const int before = g_failures;
        g_tests[i].fn();
        std::printf("[%s] %s\n", g_failures == before ? " OK " : "FAIL", g_tests[i].name);
    }
    std::printf("\n%d tests, %d checks, %d failures\n", g_testCount, g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
