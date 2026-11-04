#include "test.hpp"
#include "wifimgr/network_selector.hpp"

using namespace wifimgr;

namespace {

ScanResult bss(const std::string& ssid, int rssi, Security sec = Security::WpaPsk,
               int freq = 2437, const std::string& bssid = "00:00:00:00:00:01") {
  ScanResult r;
  r.ssid = ssid;
  r.rssi = rssi;
  r.security = sec;
  r.frequency = freq;
  r.bssid = bssid;
  return r;
}

Profile prof(const std::string& ssid, int prio = 0, Security sec = Security::WpaPsk) {
  Profile p;
  p.ssid = ssid;
  p.priority = prio;
  p.security = sec;
  p.psk = sec == Security::Open ? "" : "password123";
  return p;
}

const TimePoint kNow = TimePoint(std::chrono::seconds(1000));

}  // namespace

TEST(selects_highest_priority_then_strongest) {
  std::vector<ScanResult> scan = {bss("A", -40), bss("B", -70), bss("C", -50)};
  std::vector<Profile> ps = {prof("A", 0), prof("B", 5), prof("C", 0)};
  Blacklist bl;
  auto c = selectNetwork(scan, ps, bl, kNow, {});
  REQUIRE(c.has_value());
  CHECK_EQ(c->profile.ssid, std::string("B"));

  ps[1].priority = 0;
  c = selectNetwork(scan, ps, bl, kNow, {});
  REQUIRE(c.has_value());
  CHECK_EQ(c->profile.ssid, std::string("A"));
}

TEST(picks_strongest_bss_and_prefers_5ghz_slightly) {
  std::vector<ScanResult> scan = {bss("A", -60, Security::WpaPsk, 2437, "aa"),
                                  bss("A", -63, Security::WpaPsk, 5180, "bb"),
                                  bss("A", -70, Security::WpaPsk, 2412, "cc")};
  Blacklist bl;
  auto c = selectNetwork(scan, {prof("A")}, bl, kNow, {});
  REQUIRE(c.has_value());
  CHECK_EQ(c->bss.bssid, std::string("bb"));  // -63 + 5 beats -60
}

TEST(ignores_weak_unknown_and_non_autoconnect) {
  std::vector<ScanResult> scan = {bss("Weak", -90), bss("Stranger", -30), bss("Manual", -40)};
  std::vector<Profile> ps = {prof("Weak"), prof("Manual")};
  ps[1].autoconnect = false;
  Blacklist bl;
  CHECK(!selectNetwork(scan, ps, bl, kNow, {}).has_value());
}

TEST(refuses_security_downgrade) {
  // Same SSID as a saved PSK network, but open: possible evil twin.
  std::vector<ScanResult> scan = {bss("Home", -30, Security::Open)};
  Blacklist bl;
  CHECK(!selectNetwork(scan, {prof("Home")}, bl, kNow, {}).has_value());
  // Transition-mode AP is fine for both PSK and SAE profiles.
  scan = {bss("Home", -30, Security::WpaPskSae)};
  CHECK(selectNetwork(scan, {prof("Home")}, bl, kNow, {}).has_value());
  CHECK(selectNetwork(scan, {prof("Home", 0, Security::Sae)}, bl, kNow, {}).has_value());
  // SAE-only profile must not fall back to a PSK-only AP.
  scan = {bss("Home", -30, Security::WpaPsk)};
  CHECK(!selectNetwork(scan, {prof("Home", 0, Security::Sae)}, bl, kNow, {}).has_value());
}

TEST(exclude_skips_current_network) {
  std::vector<ScanResult> scan = {bss("A", -40), bss("B", -70)};
  Blacklist bl;
  auto c = selectNetwork(scan, {prof("A"), prof("B")}, bl, kNow, {}, "A");
  REQUIRE(c.has_value());
  CHECK_EQ(c->profile.ssid, std::string("B"));
}

TEST(blacklist_backoff_doubles_and_caps) {
  Blacklist bl(Millis(10000), Millis(35000));
  bl.recordFailure("A", kNow);
  CHECK(bl.blocked("A", kNow + Millis(9999)));
  CHECK(!bl.blocked("A", kNow + Millis(10000)));
  bl.recordFailure("A", kNow);
  CHECK(bl.blocked("A", kNow + Millis(19999)));
  CHECK(!bl.blocked("A", kNow + Millis(20000)));
  bl.recordFailure("A", kNow);
  bl.recordFailure("A", kNow);
  CHECK(bl.blocked("A", kNow + Millis(34999)));
  CHECK(!bl.blocked("A", kNow + Millis(35000)));  // capped
  CHECK_EQ(bl.failures("A"), 4);

  bl.suspend("A");
  CHECK(bl.blocked("A", kNow + Millis(10000000)));
  CHECK(bl.suspended("A"));
  bl.clear("A");
  CHECK(!bl.blocked("A", kNow));
  CHECK_EQ(bl.failures("A"), 0);
}

TEST(blacklisted_network_is_skipped_for_next_best) {
  std::vector<ScanResult> scan = {bss("A", -40), bss("B", -70)};
  Blacklist bl;
  bl.recordFailure("A", kNow);
  auto c = selectNetwork(scan, {prof("A"), prof("B")}, bl, kNow, {});
  REQUIRE(c.has_value());
  CHECK_EQ(c->profile.ssid, std::string("B"));
}
