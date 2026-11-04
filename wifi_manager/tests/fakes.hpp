// Test doubles for the manager's collaborators.
#pragma once

#include <stdlib.h>

#include <map>
#include <string>
#include <vector>

#include "wifimgr/dhcp_client.hpp"
#include "wifimgr/supplicant.hpp"
#include "wifimgr/timers.hpp"

namespace fake {

using namespace wifimgr;

class Timers : public ITimers {
 public:
  TimePoint now() const override { return now_; }
  TimerId startTimer(Millis d, std::function<void()> cb) override {
    TimerId id = next_++;
    pending_[id] = {now_ + d, std::move(cb)};
    return id;
  }
  void cancelTimer(TimerId id) override { pending_.erase(id); }

  // Advances time, firing due timers in deadline order.
  void advance(Millis d) {
    TimePoint end = now_ + d;
    for (;;) {
      auto due = pending_.end();
      for (auto it = pending_.begin(); it != pending_.end(); ++it)
        if (it->second.first <= end && (due == pending_.end() || it->second.first < due->second.first))
          due = it;
      if (due == pending_.end()) break;
      now_ = due->second.first;
      auto cb = std::move(due->second.second);
      pending_.erase(due);
      cb();
    }
    now_ = end;
  }
  size_t pendingCount() const { return pending_.size(); }

 private:
  TimePoint now_ = TimePoint(std::chrono::seconds(1000));
  TimerId next_ = 1;
  std::map<TimerId, std::pair<TimePoint, std::function<void()>>> pending_;
};

class Supplicant : public ISupplicant {
 public:
  void setEventHandler(EventHandler h) override { onEvent = std::move(h); }
  void setAvailabilityHandler(AvailabilityHandler h) override { onAvail = std::move(h); }
  bool available() const override { return up; }

  bool scan(const std::vector<std::string>& hidden) override {
    ++scans;
    lastHidden = hidden;
    return up && scanOk;
  }
  std::vector<ScanResult> scanResults() override { return results; }
  bool connect(const Profile& p) override {
    connects.push_back(p);
    link.ssid = p.ssid;
    return up && connectOk;
  }
  bool disconnect() override {
    ++disconnects;
    return true;
  }
  std::optional<LinkInfo> linkInfo() override {
    if (!up) return std::nullopt;
    return link;
  }

  // ---- test drivers
  void setUp(bool u) {
    up = u;
    if (onAvail) onAvail(u);
  }
  void event(const std::string& raw) {
    if (onEvent) onEvent(parseEvent(raw));
  }
  void deliverScan(std::vector<ScanResult> r) {
    results = std::move(r);
    event("<3>CTRL-EVENT-SCAN-RESULTS ");
  }
  void connected(const std::string& bssid = "00:11:22:33:44:55") {
    link.wpaState = "COMPLETED";
    event("<3>CTRL-EVENT-CONNECTED - Connection to " + bssid + " completed [id=0 id_str=]");
  }

  bool up = true;
  bool scanOk = true;
  bool connectOk = true;
  int scans = 0;
  int disconnects = 0;
  std::vector<std::string> lastHidden;
  std::vector<ScanResult> results;
  std::vector<Profile> connects;
  LinkInfo link;
  EventHandler onEvent;
  AvailabilityHandler onAvail;
};

class Dhcp : public IDhcpClient {
 public:
  void setExitHandler(ExitHandler h) override { onExit = std::move(h); }
  bool start() override {
    ++starts;
    run = startOk;
    return startOk;
  }
  void stop() override {
    if (run) ++stops;
    run = false;
  }
  bool running() const override { return run; }
  void exit(int code) {
    run = false;
    if (onExit) onExit(code);
  }

  bool startOk = true;
  bool run = false;
  int starts = 0;
  int stops = 0;
  ExitHandler onExit;
};

inline ScanResult bss(const std::string& ssid, int rssi, Security sec = Security::WpaPsk,
                      int freq = 2437) {
  ScanResult r;
  r.ssid = ssid;
  r.rssi = rssi;
  r.security = sec;
  r.frequency = freq;
  r.bssid = "02:00:00:00:00:" + std::to_string(10 + (rssi < 0 ? -rssi : rssi) % 89);
  return r;
}

inline std::string tempPath() {
  char tmpl[] = "/tmp/wifimgr-cm-XXXXXX";
  const char* d = mkdtemp(tmpl);
  return std::string(d ? d : "/tmp") + "/networks.conf";
}

}  // namespace fake
