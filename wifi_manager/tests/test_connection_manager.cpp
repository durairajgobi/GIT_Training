#include <memory>

#include "fakes.hpp"
#include "test.hpp"
#include "wifimgr/connection_manager.hpp"

using namespace wifimgr;
using fake::bss;

namespace {

Profile psk(const std::string& ssid, int prio = 0) {
  Profile p;
  p.ssid = ssid;
  p.psk = "password123";
  p.priority = prio;
  return p;
}

// Bundles a manager with its fakes; records emitted events.
struct Rig {
  fake::Timers timers;
  fake::Supplicant sup;
  fake::Dhcp dhcp;
  ProfileStore store{fake::tempPath()};
  ManagerConfig cfg;
  std::unique_ptr<ConnectionManager> mgr;
  std::vector<std::string> events;

  explicit Rig(std::vector<Profile> profiles = {psk("Home")}, bool startNow = true) {
    for (auto& p : profiles) store.upsert(p);
    cfg.bgScanInterval = Millis(0);
    if (startNow) start();
  }
  void start() {
    mgr = std::make_unique<ConnectionManager>(sup, dhcp, store, timers, cfg);
    mgr->setEventSink([this](const std::string& n, const Json&) { events.push_back(n); });
    mgr->start();
  }
  bool saw(const std::string& e) const {
    for (const auto& x : events)
      if (x == e) return true;
    return false;
  }
  // Drives a full successful connection to `ssid` from Scanning.
  void connectVia(std::vector<ScanResult> scan, const std::string& ip = "10.0.0.5") {
    sup.deliverScan(std::move(scan));
    sup.connected();
    mgr->onDhcpEvent("bound", ip);
  }
  ConnState state() const { return mgr->state(); }
  // Lets backoff timers run out, then waits for the next autoconnect scan.
  void waitForNextScan(Millis atLeast = Millis(400000)) {
    timers.advance(atLeast);
    for (int i = 0; i < 1000 && state() != ConnState::Scanning; ++i) timers.advance(Millis(1000));
  }
};

}  // namespace

TEST(happy_path_scan_select_associate_dhcp) {
  Rig r;
  CHECK(r.state() == ConnState::Scanning);
  CHECK_EQ(r.sup.scans, 1);

  r.sup.deliverScan({bss("Other", -30), bss("Home", -55)});
  CHECK(r.state() == ConnState::Connecting);
  REQUIRE(r.sup.connects.size() == 1);
  CHECK_EQ(r.sup.connects[0].ssid, std::string("Home"));

  r.sup.connected("aa:bb:cc:dd:ee:ff");
  CHECK(r.state() == ConnState::ObtainingIp);
  CHECK_EQ(r.dhcp.starts, 1);

  r.mgr->onDhcpEvent("bound", "192.168.1.20");
  CHECK(r.state() == ConnState::Connected);
  Json s = r.mgr->statusJson();
  CHECK_EQ(s.getString("ssid"), std::string("Home"));
  CHECK_EQ(s.getString("ip"), std::string("192.168.1.20"));
  CHECK_EQ(s.getString("bssid"), std::string("aa:bb:cc:dd:ee:ff"));
  CHECK(r.saw("state"));
}

TEST(no_known_network_backs_off_exponentially) {
  Rig r;
  r.sup.deliverScan({bss("Stranger", -40)});
  CHECK(r.state() == ConnState::Idle);
  r.timers.advance(Millis(4999));
  CHECK_EQ(r.sup.scans, 1);
  r.timers.advance(Millis(1));
  CHECK_EQ(r.sup.scans, 2);
  CHECK(r.state() == ConnState::Scanning);
  r.sup.deliverScan({});
  r.timers.advance(Millis(9999));
  CHECK_EQ(r.sup.scans, 2);
  r.timers.advance(Millis(1));
  CHECK_EQ(r.sup.scans, 3);  // second wait doubled to 10 s
}

TEST(scan_timeout_goes_idle_and_retries) {
  Rig r;
  r.timers.advance(r.cfg.scanTimeout);
  CHECK(r.state() == ConnState::Idle);
  r.timers.advance(r.cfg.scanIntervalMin);
  CHECK_EQ(r.sup.scans, 2);
}

TEST(no_saved_networks_means_no_scanning_until_one_is_added) {
  Rig r({});
  CHECK(r.state() == ConnState::Idle);
  CHECK_EQ(r.sup.scans, 0);
  r.timers.advance(Millis(600000));
  CHECK_EQ(r.sup.scans, 0);

  CHECK(r.mgr->saveProfile(psk("Home")).ok);
  CHECK(r.state() == ConnState::Scanning);
  CHECK_EQ(r.sup.scans, 1);
}

TEST(save_rejects_invalid_profiles) {
  Rig r;
  Profile bad = psk("Home");
  bad.psk = "short";
  OpResult res = r.mgr->saveProfile(bad);
  CHECK(!res.ok);
  CHECK(r.store.find("Home")->psk == "password123");
}

TEST(hidden_profiles_are_probed_in_scans) {
  Profile h = psk("Secret");
  h.hidden = true;
  Rig r({psk("Home"), h});
  REQUIRE(r.sup.lastHidden.size() == 1);
  CHECK_EQ(r.sup.lastHidden[0], std::string("Secret"));
}

TEST(wrong_key_is_retried_then_suspended) {
  Rig r({psk("Home")});
  for (int attempt = 1; attempt <= 3; ++attempt) {
    r.sup.deliverScan({bss("Home", -50)});
    REQUIRE(r.state() == ConnState::Connecting);
    r.sup.event("<3>CTRL-EVENT-SSID-TEMP-DISABLED id=0 ssid=\"Home\" auth_failures=1 duration=10 reason=WRONG_KEY");
    CHECK(r.state() == ConnState::Idle || r.state() == ConnState::Scanning);
    if (attempt < 3) r.waitForNextScan();
  }
  CHECK(r.saw("credentials_rejected"));
  size_t connectsBefore = r.sup.connects.size();
  r.waitForNextScan();
  r.sup.deliverScan({bss("Home", -50)});
  CHECK_EQ(r.sup.connects.size(), connectsBefore);  // suspended: not retried
  CHECK_EQ(r.mgr->profilesJson().items()[0].getString("status"),
           std::string("credentials_rejected"));

  // New credentials lift the suspension.
  Profile fixed = psk("Home");
  fixed.psk = "correct-password";
  CHECK(r.mgr->saveProfile(fixed).ok);
  REQUIRE(r.state() == ConnState::Scanning);
  r.sup.deliverScan({bss("Home", -50)});
  CHECK(r.state() == ConnState::Connecting);
  CHECK_EQ(r.sup.connects.back().psk, std::string("correct-password"));
}

TEST(connect_timeout_blacklists_and_falls_back_to_next_network) {
  Rig r({psk("A", 5), psk("B", 1)});
  r.sup.deliverScan({bss("A", -50), bss("B", -60)});
  REQUIRE(r.sup.connects.back().ssid == "A");
  r.timers.advance(r.cfg.connectTimeout);
  CHECK(r.saw("connect_failed"));
  CHECK(r.state() == ConnState::Scanning);  // immediate rescan
  CHECK(r.sup.disconnects >= 1);
  r.sup.deliverScan({bss("A", -50), bss("B", -60)});
  CHECK_EQ(r.sup.connects.back().ssid, std::string("B"));
  CHECK(r.mgr->statusJson().getString("last_error").find("timed out") != std::string::npos);
}

TEST(network_not_found_fails_fast) {
  Rig r;
  r.sup.deliverScan({bss("Home", -50)});
  r.sup.event("<3>CTRL-EVENT-NETWORK-NOT-FOUND");
  CHECK(r.state() != ConnState::Connecting);
  CHECK(r.saw("connect_failed"));
}

TEST(assoc_rejects_tolerated_until_third) {
  Rig r;
  r.sup.deliverScan({bss("Home", -50)});
  r.sup.event("<3>CTRL-EVENT-ASSOC-REJECT bssid=00:11:22:33:44:55 status_code=17");
  r.sup.event("<3>CTRL-EVENT-ASSOC-REJECT bssid=00:11:22:33:44:55 status_code=17");
  CHECK(r.state() == ConnState::Connecting);
  r.sup.event("<3>CTRL-EVENT-ASSOC-REJECT bssid=00:11:22:33:44:55 status_code=17");
  CHECK(r.state() != ConnState::Connecting);
}

TEST(disconnect_events_while_connecting_are_ignored) {
  Rig r;
  r.sup.deliverScan({bss("Home", -50)});
  r.sup.event("<3>CTRL-EVENT-DISCONNECTED bssid=00:11:22:33:44:55 reason=3 locally_generated=1");
  CHECK(r.state() == ConnState::Connecting);
}

TEST(stale_connected_event_for_previous_network_is_ignored) {
  Rig r;
  r.sup.deliverScan({bss("Home", -50)});
  r.sup.link.ssid = "Previous";
  r.sup.connected();
  CHECK(r.state() == ConnState::Connecting);
  r.sup.link.ssid = "Home";
  r.sup.connected();
  CHECK(r.state() == ConnState::ObtainingIp);
}

TEST(dhcp_timeout_fails_attempt_and_stops_client) {
  Rig r;
  r.sup.deliverScan({bss("Home", -50)});
  r.sup.connected();
  REQUIRE(r.dhcp.running());
  r.timers.advance(r.cfg.dhcpTimeout);
  CHECK(!r.dhcp.running());
  CHECK(r.saw("connect_failed"));
}

TEST(dhcp_client_exit_while_obtaining_fails_and_while_connected_restarts) {
  Rig r;
  r.sup.deliverScan({bss("Home", -50)});
  r.sup.connected();
  r.dhcp.exit(1);
  CHECK(r.saw("connect_failed"));

  r.waitForNextScan();
  r.connectVia({bss("Home", -50)});
  REQUIRE(r.state() == ConnState::Connected);
  int starts = r.dhcp.starts;
  r.dhcp.exit(1);
  CHECK(r.state() == ConnState::ObtainingIp);
  CHECK_EQ(r.dhcp.starts, starts + 1);
}

TEST(netlink_address_completes_connection) {
  Rig r;
  r.sup.deliverScan({bss("Home", -50)});
  r.sup.connected();
  r.mgr->onAddressChange(true, "10.1.2.3");
  CHECK(r.state() == ConnState::Connected);
  CHECK_EQ(r.mgr->statusJson().getString("ip"), std::string("10.1.2.3"));
  r.mgr->onAddressChange(false, "10.1.2.3");
  CHECK(r.state() == ConnState::ObtainingIp);
}

TEST(lease_loss_returns_to_obtaining_ip) {
  Rig r;
  r.connectVia({bss("Home", -50)});
  r.mgr->onDhcpEvent("deconfig", "");
  CHECK(r.state() == ConnState::ObtainingIp);
  r.mgr->onDhcpEvent("bound", "10.0.0.9");
  CHECK(r.state() == ConnState::Connected);
  CHECK_EQ(r.mgr->statusJson().getString("ip"), std::string("10.0.0.9"));
}

TEST(link_loss_waits_for_supplicant_reassociation) {
  Rig r;
  r.connectVia({bss("Home", -50)});
  int connects = static_cast<int>(r.sup.connects.size());
  r.sup.event("<3>CTRL-EVENT-DISCONNECTED bssid=00:11:22:33:44:55 reason=4");
  CHECK(r.state() == ConnState::Connecting);
  CHECK(r.saw("link_lost"));
  CHECK(!r.dhcp.running());
  r.sup.connected();  // wpa_supplicant got it back by itself
  CHECK(r.state() == ConnState::ObtainingIp);
  CHECK_EQ(static_cast<int>(r.sup.connects.size()), connects);  // no reconfiguration
}

TEST(link_loss_without_recovery_times_out) {
  Rig r;
  r.connectVia({bss("Home", -50)});
  r.sup.event("<3>CTRL-EVENT-DISCONNECTED bssid=00:11:22:33:44:55 reason=4");
  r.timers.advance(r.cfg.connectTimeout);
  CHECK(r.saw("connect_failed"));
}

TEST(user_connect_reports_success_after_lease) {
  Rig r({psk("Home"), psk("Work")});
  int calls = 0;
  bool ok = false;
  r.mgr->connect("Work", [&](const OpResult& res) {
    ++calls;
    ok = res.ok;
  });
  CHECK(r.state() == ConnState::Connecting);
  CHECK_EQ(r.sup.connects.back().ssid, std::string("Work"));
  r.sup.connected();
  CHECK_EQ(calls, 0);
  r.mgr->onDhcpEvent("bound", "10.0.0.7");
  CHECK_EQ(calls, 1);
  CHECK(ok);
}

TEST(user_connect_reports_wrong_key) {
  Rig r;
  std::string error;
  r.mgr->connect("Home", [&](const OpResult& res) { error = res.error; });
  r.sup.event("<3>CTRL-EVENT-SSID-TEMP-DISABLED id=0 ssid=\"Home\" auth_failures=1 duration=10 reason=WRONG_KEY");
  CHECK(error.find("authentication failed") != std::string::npos);
}

TEST(user_connect_to_unknown_network_fails_immediately) {
  Rig r;
  std::string error;
  r.mgr->connect("Nope", [&](const OpResult& res) { error = res.error; });
  CHECK(error.find("unknown network") != std::string::npos);
}

TEST(second_connect_supersedes_first) {
  Rig r({psk("Home"), psk("Work")});
  std::string first;
  r.mgr->connect("Home", [&](const OpResult& res) { first = res.error; });
  r.mgr->connect("Work", [](const OpResult&) {});
  CHECK(first.find("superseded") != std::string::npos);
  CHECK_EQ(r.sup.connects.back().ssid, std::string("Work"));
}

TEST(disconnect_pauses_autoconnect_until_reconnect) {
  Rig r;
  r.connectVia({bss("Home", -50)});
  r.mgr->disconnect();
  CHECK(r.state() == ConnState::Idle);
  CHECK(r.mgr->statusJson().getBool("paused"));
  CHECK(!r.dhcp.running());
  int scans = r.sup.scans;
  r.timers.advance(Millis(600000));
  CHECK_EQ(r.sup.scans, scans);
  r.mgr->reconnect();
  CHECK(r.state() == ConnState::Scanning);
}

TEST(autoconnect_off_stops_background_retries) {
  Rig r;
  r.sup.deliverScan({});
  r.mgr->setAutoconnect(false);
  int scans = r.sup.scans;
  r.timers.advance(Millis(600000));
  CHECK_EQ(r.sup.scans, scans);
  r.mgr->setAutoconnect(true);
  CHECK_EQ(r.sup.scans, scans + 1);
}

TEST(supplicant_loss_and_recovery) {
  Rig r;
  std::string error;
  r.mgr->connect("Home", [&](const OpResult& res) { error = res.error; });
  r.sup.connected();
  REQUIRE(r.dhcp.running());
  r.sup.setUp(false);
  CHECK(r.state() == ConnState::Unavailable);
  CHECK(!r.dhcp.running());
  CHECK(error.find("unavailable") != std::string::npos);
  r.sup.setUp(true);
  CHECK(r.state() == ConnState::Scanning);
}

TEST(starts_unavailable_when_supplicant_down) {
  Rig r({psk("Home")}, false);
  r.sup.up = false;
  r.start();
  CHECK(r.state() == ConnState::Unavailable);
  bool scanned = true;
  r.mgr->requestScan([&](bool ok) { scanned = ok; });
  CHECK(!scanned);
}

TEST(forget_current_network_disconnects) {
  Rig r;
  r.connectVia({bss("Home", -50)});
  CHECK(r.mgr->forget("Home").ok);
  CHECK(r.state() == ConnState::Idle);
  CHECK(!r.dhcp.running());
  CHECK(r.store.find("Home") == nullptr);
  CHECK(!r.mgr->forget("Home").ok);
}

TEST(user_scan_request_completes_with_results) {
  Rig r;
  r.connectVia({bss("Home", -50)});
  int done = 0;
  r.mgr->requestScan([&](bool ok) { done += ok ? 1 : 100; });
  r.mgr->requestScan([&](bool ok) { done += ok ? 1 : 100; });
  r.sup.deliverScan({bss("Home", -50), bss("X", -70)});
  CHECK_EQ(done, 2);
  CHECK_EQ(r.mgr->scanResultsJson().items().size(), size_t(2));
  CHECK(r.state() == ConnState::Connected);
}

TEST(switches_to_higher_priority_network_when_it_appears) {
  Rig r({psk("Low", 1), psk("High", 10)});
  r.connectVia({bss("Low", -50)});
  REQUIRE(r.state() == ConnState::Connected);
  r.mgr->requestScan([](bool) {});
  r.sup.deliverScan({bss("Low", -50), bss("High", -65)});
  CHECK(r.saw("switching"));
  CHECK_EQ(r.sup.connects.back().ssid, std::string("High"));
  CHECK(r.state() == ConnState::Connecting);
}

TEST(weak_signal_roams_only_with_enough_hysteresis) {
  Rig r({psk("A"), psk("B")});
  r.sup.link.rssi = -82;
  r.connectVia({bss("A", -60)});  // polls signal on connect: -82 < -75 -> scan
  int scans = r.sup.scans;
  CHECK(scans >= 2);
  r.sup.deliverScan({bss("A", -82), bss("B", -78)});  // only 4 dB better
  CHECK(r.state() == ConnState::Connected);
  CHECK(!r.saw("switching"));
  r.timers.advance(Millis(30000));  // poll + roam scan rate limit expire
  r.sup.deliverScan({bss("A", -82), bss("B", -60)});
  CHECK(r.saw("switching"));
  CHECK_EQ(r.sup.connects.back().ssid, std::string("B"));
}

TEST(status_json_never_contains_secrets) {
  Rig r;
  r.connectVia({bss("Home", -50)});
  std::string all = r.mgr->statusJson().dump() + r.mgr->profilesJson().dump();
  CHECK(all.find("password123") == std::string::npos);
}
