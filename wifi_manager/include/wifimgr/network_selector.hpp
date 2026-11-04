// Chooses which saved network to join from a set of scan results, and keeps
// per-SSID failure backoff.
#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "wifimgr/timers.hpp"
#include "wifimgr/types.hpp"

namespace wifimgr {

// Exponential backoff per SSID after failed connection attempts, plus a
// "suspended" state for networks whose credentials were rejected repeatedly
// (retrying a wrong password only locks the account / spams the AP).
class Blacklist {
 public:
  Blacklist(Millis base = Millis(10000), Millis max = Millis(300000)) : base_(base), max_(max) {}

  void recordFailure(const std::string& ssid, TimePoint now);
  void suspend(const std::string& ssid) { entries_[ssid].suspended = true; }
  void clear(const std::string& ssid) { entries_.erase(ssid); }
  void clearAll() { entries_.clear(); }

  bool blocked(const std::string& ssid, TimePoint now) const;
  bool suspended(const std::string& ssid) const;
  int failures(const std::string& ssid) const;
  std::optional<TimePoint> blockedUntil(const std::string& ssid) const;

 private:
  struct Entry {
    int failures = 0;
    TimePoint until{};
    bool suspended = false;
  };
  Millis base_, max_;
  std::map<std::string, Entry> entries_;
};

struct SelectionPolicy {
  int minRssi = -85;     // ignore BSSs weaker than this (dBm)
  int band5GhzBonus = 5; // dB credit for 5/6 GHz when comparing signal
};

struct Candidate {
  Profile profile;
  ScanResult bss;   // strongest compatible BSS for this profile
  int score = 0;    // rssi + band bonus
};

// Picks the best saved, autoconnect-enabled, non-blacklisted network visible
// in `scan`. Order: priority, then score, then SSID (deterministic).
// `exclude` skips one SSID (used when evaluating a roam away from it).
std::optional<Candidate> selectNetwork(const std::vector<ScanResult>& scan,
                                       const std::vector<Profile>& profiles,
                                       const Blacklist& blacklist, TimePoint now,
                                       const SelectionPolicy& policy,
                                       const std::string& exclude = {});

// Best visible compatible BSS for a given profile, if any.
std::optional<ScanResult> bestBssFor(const std::vector<ScanResult>& scan, const Profile& p);

}  // namespace wifimgr
