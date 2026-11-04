#include "wifimgr/network_selector.hpp"

#include <algorithm>

namespace wifimgr {

void Blacklist::recordFailure(const std::string& ssid, TimePoint now) {
  Entry& e = entries_[ssid];
  e.failures++;
  Millis delay = base_;
  for (int i = 1; i < e.failures && delay < max_; ++i) delay *= 2;
  e.until = now + std::min(delay, max_);
}

bool Blacklist::blocked(const std::string& ssid, TimePoint now) const {
  auto it = entries_.find(ssid);
  if (it == entries_.end()) return false;
  return it->second.suspended || now < it->second.until;
}

bool Blacklist::suspended(const std::string& ssid) const {
  auto it = entries_.find(ssid);
  return it != entries_.end() && it->second.suspended;
}

int Blacklist::failures(const std::string& ssid) const {
  auto it = entries_.find(ssid);
  return it == entries_.end() ? 0 : it->second.failures;
}

std::optional<TimePoint> Blacklist::blockedUntil(const std::string& ssid) const {
  auto it = entries_.find(ssid);
  if (it == entries_.end() || it->second.failures == 0) return std::nullopt;
  return it->second.until;
}

std::optional<ScanResult> bestBssFor(const std::vector<ScanResult>& scan, const Profile& p) {
  std::optional<ScanResult> best;
  for (const auto& r : scan) {
    if (r.ssid != p.ssid || !securityCompatible(p.security, r.security)) continue;
    if (!best || r.rssi > best->rssi) best = r;
  }
  return best;
}

std::optional<Candidate> selectNetwork(const std::vector<ScanResult>& scan,
                                       const std::vector<Profile>& profiles,
                                       const Blacklist& blacklist, TimePoint now,
                                       const SelectionPolicy& policy,
                                       const std::string& exclude) {
  std::optional<Candidate> best;
  for (const auto& p : profiles) {
    if (!p.autoconnect || p.ssid == exclude || blacklist.blocked(p.ssid, now)) continue;
    std::optional<Candidate> c;
    for (const auto& r : scan) {
      if (r.ssid != p.ssid || !securityCompatible(p.security, r.security)) continue;
      if (r.rssi < policy.minRssi) continue;
      int score = r.rssi + (r.frequency >= 4900 ? policy.band5GhzBonus : 0);
      if (!c || score > c->score) c = Candidate{p, r, score};
    }
    if (!c) continue;
    auto better = [](const Candidate& a, const Candidate& b) {
      if (a.profile.priority != b.profile.priority) return a.profile.priority > b.profile.priority;
      if (a.score != b.score) return a.score > b.score;
      return a.profile.ssid < b.profile.ssid;
    };
    if (!best || better(*c, *best)) best = std::move(c);
  }
  return best;
}

}  // namespace wifimgr
