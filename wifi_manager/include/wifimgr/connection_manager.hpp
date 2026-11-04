// Station-mode connection policy.
//
//            supplicant up            scan results, candidate
//  Unavailable ─────────► Idle ──► Scanning ─────────────────► Connecting
//       ▲                  ▲ ▲         │ no candidate               │ CONNECTED
//       │ supplicant lost  │ └─backoff─┘                            ▼
//       └──── (any) ───────┘                                   ObtainingIp
//                          ▲  failure: timeout / wrong key /        │ lease
//                          └──── not found / DHCP timeout ◄─────────┤
//                                                                   ▼
//                 link lost: back to Connecting (wpa_supplicant  Connected
//                 retries the same network until connect_timeout)
//
// wpa_supplicant only ever holds the one network this class selected, so
// all policy (priorities, backoff, roaming between SSIDs, downgrade
// protection) lives here and is unit-tested against fakes.
#pragma once

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "wifimgr/dhcp_client.hpp"
#include "wifimgr/json.hpp"
#include "wifimgr/network_selector.hpp"
#include "wifimgr/profile_store.hpp"
#include "wifimgr/supplicant.hpp"
#include "wifimgr/timers.hpp"
#include "wifimgr/types.hpp"

namespace wifimgr {

struct ManagerConfig {
  bool autoconnect = true;
  SelectionPolicy selection;
  Millis scanIntervalMin{5000};    // first retry after an unsuccessful scan
  Millis scanIntervalMax{120000};  // backoff cap while no known network is in range
  Millis scanTimeout{15000};
  Millis connectTimeout{25000};    // association + authentication
  Millis dhcpTimeout{30000};
  Millis signalPollInterval{10000};
  Millis bgScanInterval{180000};   // look for better networks while connected; 0 = off
  int roamRssiThreshold = -75;     // below this, consider roaming to another SSID
  int roamHysteresis = 10;         // dB a candidate must beat the current link by
  int maxAuthFailures = 3;         // then suspend the profile until its credentials change
  Millis blacklistBase{10000};
  Millis blacklistMax{300000};
};

struct OpResult {
  bool ok = true;
  std::string error;
  static OpResult success() { return {}; }
  static OpResult failure(std::string e) { return {false, std::move(e)}; }
};

class ConnectionManager {
 public:
  using EventSink = std::function<void(const std::string& name, const Json& data)>;
  using ScanCallback = std::function<void(bool ok)>;
  using ConnectCallback = std::function<void(const OpResult&)>;

  ConnectionManager(ISupplicant& supplicant, IDhcpClient& dhcp, ProfileStore& store,
                    ITimers& timers, ManagerConfig cfg);
  ~ConnectionManager();

  void start();
  void setEventSink(EventSink sink) { sink_ = std::move(sink); }

  // ---- queries
  ConnState state() const { return state_; }
  const std::vector<ScanResult>& scanResults() const { return scan_; }
  std::optional<Profile> findProfile(const std::string& ssid) const {
    const Profile* p = store_.find(ssid);
    return p ? std::optional<Profile>(*p) : std::nullopt;
  }
  Json statusJson() const;
  Json scanResultsJson() const;
  Json profilesJson() const;

  // ---- control-socket commands
  void requestScan(ScanCallback cb);
  OpResult saveProfile(const Profile& p);
  OpResult forget(const std::string& ssid);
  // Completes when an IP is obtained or the attempt fails.
  void connect(const std::string& ssid, ConnectCallback cb);
  void disconnect();  // and stay disconnected until connect/reconnect
  void reconnect();
  void setAutoconnect(bool on);

  // ---- inputs from the environment
  void onSupplicantEvent(const SupplicantEvent& ev);
  void onSupplicantAvailable(bool available);
  void onDhcpEvent(const std::string& event, const std::string& ip);
  void onDhcpExit(int code);
  void onAddressChange(bool added, const std::string& ip);

 private:
  void setState(ConnState s);
  void emit(const std::string& name, Json data);
  bool canAutoconnect() const;
  bool hasAutoconnectProfiles() const;

  void startScan();
  bool issueScan();
  void onScanTimeout();
  void onScanDone(bool ok);
  void trySelect();
  void goIdleWithBackoff();

  void beginConnect(const Profile& p);
  void startDhcp();
  void onLease(const std::string& ip);
  void onLeaseLost(const char* why);
  void onLinkLost();
  void attemptFailed(const std::string& reason);
  void teardown();  // stop DHCP, timers and the supplicant network
  void finishPendingConnect(const OpResult& r);

  void pollSignal();
  void backgroundScan();
  void evaluateRoam();

  ISupplicant& sup_;
  IDhcpClient& dhcp_;
  ProfileStore& store_;
  ITimers& timers_;
  ManagerConfig cfg_;
  EventSink sink_;

  ConnState state_ = ConnState::Unavailable;
  bool autoconnect_;
  bool userDisconnected_ = false;
  std::optional<Profile> current_;  // network being joined / joined
  std::string bssid_;
  std::string ip_;
  int rssi_ = -100;
  int frequency_ = 0;
  std::string lastError_;

  std::vector<ScanResult> scan_;
  bool scanPending_ = false;
  std::vector<ScanCallback> scanWaiters_;
  int idleStep_ = 0;
  TimePoint lastRoamScan_{};

  ConnectCallback pendingConnect_;
  int assocRejects_ = 0;
  Blacklist blacklist_;
  std::map<std::string, int> authFailures_;

  Timer stateTimer_;  // connect / DHCP timeout
  Timer retryTimer_;  // next autoconnect scan
  Timer scanTimer_;   // scan result timeout
  Timer pollTimer_;
  Timer bgScanTimer_;
};

}  // namespace wifimgr
