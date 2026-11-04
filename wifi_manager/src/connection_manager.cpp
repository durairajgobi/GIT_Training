#include "wifimgr/connection_manager.hpp"

#include <algorithm>

#include "wifimgr/log.hpp"

namespace wifimgr {
namespace {

constexpr Millis kRoamScanMinInterval{30000};

}  // namespace

ConnectionManager::ConnectionManager(ISupplicant& supplicant, IDhcpClient& dhcp,
                                     ProfileStore& store, ITimers& timers, ManagerConfig cfg)
    : sup_(supplicant),
      dhcp_(dhcp),
      store_(store),
      timers_(timers),
      cfg_(cfg),
      autoconnect_(cfg.autoconnect),
      blacklist_(cfg.blacklistBase, cfg.blacklistMax),
      stateTimer_(timers),
      retryTimer_(timers),
      scanTimer_(timers),
      pollTimer_(timers),
      bgScanTimer_(timers) {}

ConnectionManager::~ConnectionManager() {
  sup_.setEventHandler(nullptr);
  sup_.setAvailabilityHandler(nullptr);
  dhcp_.setExitHandler(nullptr);
}

void ConnectionManager::start() {
  sup_.setEventHandler([this](const SupplicantEvent& ev) { onSupplicantEvent(ev); });
  sup_.setAvailabilityHandler([this](bool up) { onSupplicantAvailable(up); });
  dhcp_.setExitHandler([this](int code) { onDhcpExit(code); });
  if (sup_.available()) onSupplicantAvailable(true);
}

// ---------------------------------------------------------------- helpers

void ConnectionManager::setState(ConnState s) {
  if (s == state_) return;
  LOGI("state %s -> %s%s%s", toString(state_), toString(s), current_ ? " ssid=" : "",
       current_ ? current_->ssid.c_str() : "");
  state_ = s;
  emit("state", statusJson());
}

void ConnectionManager::emit(const std::string& name, Json data) {
  if (sink_) sink_(name, data);
}

bool ConnectionManager::hasAutoconnectProfiles() const {
  return std::any_of(store_.profiles().begin(), store_.profiles().end(),
                     [](const Profile& p) { return p.autoconnect; });
}

bool ConnectionManager::canAutoconnect() const {
  return autoconnect_ && !userDisconnected_ && sup_.available();
}

void ConnectionManager::finishPendingConnect(const OpResult& r) {
  if (!pendingConnect_) return;
  auto cb = std::move(pendingConnect_);
  pendingConnect_ = nullptr;
  cb(r);
}

void ConnectionManager::teardown() {
  stateTimer_.cancel();
  pollTimer_.cancel();
  bgScanTimer_.cancel();
  dhcp_.stop();
  if (current_ && sup_.available()) sup_.disconnect();
  current_.reset();
  bssid_.clear();
  ip_.clear();
  rssi_ = -100;
  frequency_ = 0;
}

// ---------------------------------------------------------------- scanning

void ConnectionManager::startScan() {
  if (state_ != ConnState::Idle && state_ != ConnState::Scanning) return;
  retryTimer_.cancel();
  if (!hasAutoconnectProfiles()) {
    // Nothing to join; do not keep the radio busy. saveProfile() re-kicks.
    LOGI("no saved networks with autoconnect; waiting");
    setState(ConnState::Idle);
    return;
  }
  setState(ConnState::Scanning);
  if (!issueScan()) onScanDone(false);
}

bool ConnectionManager::issueScan() {
  if (scanPending_) return true;
  std::vector<std::string> hidden;
  for (const auto& p : store_.profiles())
    if (p.hidden) hidden.push_back(p.ssid);
  if (!sup_.scan(hidden)) {
    LOGW("scan request rejected");
    return false;
  }
  scanPending_ = true;
  scanTimer_.start(cfg_.scanTimeout, [this] { onScanTimeout(); });
  return true;
}

void ConnectionManager::onScanTimeout() {
  LOGW("scan timed out");
  scanPending_ = false;
  onScanDone(false);
}

void ConnectionManager::onScanDone(bool ok) {
  if (state_ == ConnState::Scanning) {
    if (ok) trySelect();
    else goIdleWithBackoff();
  } else if (state_ == ConnState::Connected && ok) {
    evaluateRoam();
  }
  auto waiters = std::move(scanWaiters_);
  scanWaiters_.clear();
  for (auto& w : waiters) w(ok);
}

void ConnectionManager::trySelect() {
  auto cand = selectNetwork(scan_, store_.profiles(), blacklist_, timers_.now(), cfg_.selection);
  if (!cand) {
    LOGI("no usable saved network in range (%zu BSSs seen)", scan_.size());
    goIdleWithBackoff();
    return;
  }
  LOGI("selected '%s' (priority %d, %d dBm, %d MHz)", cand->profile.ssid.c_str(),
       cand->profile.priority, cand->bss.rssi, cand->bss.frequency);
  beginConnect(cand->profile);
}

void ConnectionManager::goIdleWithBackoff() {
  setState(ConnState::Idle);
  if (!canAutoconnect() || !hasAutoconnectProfiles()) return;
  Millis delay = cfg_.scanIntervalMin;
  for (int i = 0; i < idleStep_ && delay < cfg_.scanIntervalMax; ++i) delay *= 2;
  delay = std::min(delay, cfg_.scanIntervalMax);
  idleStep_++;
  LOGD("next scan in %lld ms", static_cast<long long>(delay.count()));
  retryTimer_.start(delay, [this] { startScan(); });
}

// ---------------------------------------------------------------- connecting

void ConnectionManager::beginConnect(const Profile& p) {
  retryTimer_.cancel();
  stateTimer_.cancel();
  pollTimer_.cancel();
  bgScanTimer_.cancel();
  dhcp_.stop();
  ip_.clear();
  bssid_.clear();
  assocRejects_ = 0;
  current_ = p;
  if (!sup_.connect(p)) {
    attemptFailed("wpa_supplicant rejected the network configuration");
    return;
  }
  setState(ConnState::Connecting);
  stateTimer_.start(cfg_.connectTimeout, [this] { attemptFailed("association timed out"); });
}

void ConnectionManager::startDhcp() {
  if (!dhcp_.start()) {
    attemptFailed("could not start DHCP client");
    return;
  }
  stateTimer_.start(cfg_.dhcpTimeout, [this] { attemptFailed("no DHCP lease"); });
}

void ConnectionManager::onLease(const std::string& ip) {
  if (state_ != ConnState::ObtainingIp && state_ != ConnState::Connected) return;
  if (ip.empty()) return;
  bool changed = ip != ip_;
  ip_ = ip;
  if (state_ == ConnState::Connected) {
    if (changed) emit("state", statusJson());
    return;
  }
  stateTimer_.cancel();
  blacklist_.clear(current_->ssid);
  authFailures_.erase(current_->ssid);
  idleStep_ = 0;
  lastError_.clear();
  setState(ConnState::Connected);
  pollSignal();
  if (cfg_.bgScanInterval.count() > 0)
    bgScanTimer_.start(cfg_.bgScanInterval, [this] { backgroundScan(); });
  finishPendingConnect(OpResult::success());
}

void ConnectionManager::onLeaseLost(const char* why) {
  if (state_ != ConnState::Connected) return;
  LOGW("lost IPv4 address (%s)", why);
  ip_.clear();
  pollTimer_.cancel();
  bgScanTimer_.cancel();
  setState(ConnState::ObtainingIp);
  if (!dhcp_.running()) {
    startDhcp();
  } else {
    stateTimer_.start(cfg_.dhcpTimeout, [this] { attemptFailed("no DHCP lease"); });
  }
}

void ConnectionManager::onLinkLost() {
  LOGW("link to '%s' lost; waiting for wpa_supplicant to reassociate", current_->ssid.c_str());
  dhcp_.stop();
  ip_.clear();
  bssid_.clear();
  pollTimer_.cancel();
  bgScanTimer_.cancel();
  emit("link_lost", Json::object().set("ssid", current_->ssid));
  setState(ConnState::Connecting);
  stateTimer_.start(cfg_.connectTimeout, [this] { attemptFailed("link lost, reconnect timed out"); });
}

void ConnectionManager::attemptFailed(const std::string& reason) {
  std::string ssid = current_ ? current_->ssid : std::string();
  LOGW("connection to '%s' failed: %s", ssid.c_str(), reason.c_str());
  teardown();
  if (!ssid.empty()) blacklist_.recordFailure(ssid, timers_.now());
  lastError_ = ssid + ": " + reason;
  emit("connect_failed", Json::object().set("ssid", ssid).set("reason", reason));
  finishPendingConnect(OpResult::failure(reason));
  setState(ConnState::Idle);
  if (canAutoconnect()) startScan();
}

// ---------------------------------------------------------------- events

void ConnectionManager::onSupplicantAvailable(bool available) {
  if (available) {
    LOGI("wpa_supplicant available");
    emit("supplicant", Json::object().set("available", true));
    if (state_ == ConnState::Unavailable) {
      setState(ConnState::Idle);
      if (canAutoconnect()) startScan();
    }
    return;
  }
  emit("supplicant", Json::object().set("available", false));
  retryTimer_.cancel();
  scanTimer_.cancel();
  scanPending_ = false;
  // wpa_supplicant is gone, so do not try to talk to it in teardown().
  current_.reset();
  teardown();
  finishPendingConnect(OpResult::failure("wpa_supplicant unavailable"));
  setState(ConnState::Unavailable);
  auto waiters = std::move(scanWaiters_);
  scanWaiters_.clear();
  for (auto& w : waiters) w(false);
}

void ConnectionManager::onSupplicantEvent(const SupplicantEvent& ev) {
  using T = SupplicantEvent::Type;
  switch (ev.type) {
    case T::ScanResults:
      scanTimer_.cancel();
      scanPending_ = false;
      scan_ = sup_.scanResults();
      emit("scan_results", Json::object().set("count", static_cast<long>(scan_.size())));
      onScanDone(true);
      break;

    case T::ScanFailed:
      if (scanPending_) {
        scanTimer_.cancel();
        scanPending_ = false;
        onScanDone(false);
      }
      break;

    case T::Connected:
      bssid_ = ev.bssid;
      if (state_ == ConnState::Connecting) {
        // A CONNECTED for the network we just replaced can still be queued;
        // only accept the one for the SSID we are actually joining.
        if (auto li = sup_.linkInfo(); li && !li->ssid.empty() && li->ssid != current_->ssid) {
          LOGW("ignoring stale CONNECTED for '%s'", li->ssid.c_str());
          break;
        }
        LOGI("associated with %s", ev.bssid.c_str());
        stateTimer_.cancel();
        setState(ConnState::ObtainingIp);
        startDhcp();
      } else if (state_ == ConnState::ObtainingIp || state_ == ConnState::Connected) {
        LOGI("roamed to %s", ev.bssid.c_str());  // BSS transition inside the ESS
        emit("roamed", Json::object().set("bssid", ev.bssid));
      }
      break;

    case T::Disconnected:
      // While Connecting, disconnects are part of wpa_supplicant's retries
      // (and our own REMOVE_NETWORK); the connect timeout bounds them.
      if (state_ == ConnState::ObtainingIp || state_ == ConnState::Connected) onLinkLost();
      break;

    case T::AuthFailure: {
      if (!current_ || (state_ != ConnState::Connecting && state_ != ConnState::ObtainingIp))
        break;
      if (!ev.ssid.empty() && ev.ssid != current_->ssid) break;
      std::string ssid = current_->ssid;
      int n = ++authFailures_[ssid];
      emit("auth_failure", Json::object().set("ssid", ssid).set("attempts", n));
      if (n >= cfg_.maxAuthFailures) {
        // Suspend first so attemptFailed()'s rescan cannot pick it again.
        LOGW("'%s' rejected credentials %d times; autoconnect suspended until they change",
             ssid.c_str(), n);
        blacklist_.suspend(ssid);
        emit("credentials_rejected", Json::object().set("ssid", ssid));
      }
      attemptFailed("authentication failed (" + (ev.reason.empty() ? "wrong key" : ev.reason) + ")");
      break;
    }

    case T::ConnFailed:
      if (state_ == ConnState::Connecting && ++assocRejects_ >= 3)
        attemptFailed("rejected by access point (" + ev.reason + ")");
      break;

    case T::NetworkNotFound:
      if (state_ == ConnState::Connecting) attemptFailed("network not found");
      break;

    case T::Terminating:  // the supplicant wrapper reports availability(false)
    case T::Other:
      break;
  }
}

void ConnectionManager::onDhcpEvent(const std::string& event, const std::string& ip) {
  LOGI("dhcp event %s %s", event.c_str(), ip.c_str());
  if (event == "bound" || event == "renew") onLease(ip);
  else if (event == "deconfig" || event == "leasefail" || event == "nak") onLeaseLost(event.c_str());
}

void ConnectionManager::onDhcpExit(int code) {
  if (state_ == ConnState::ObtainingIp) {
    attemptFailed("DHCP client exited with status " + std::to_string(code));
  } else if (state_ == ConnState::Connected) {
    LOGW("DHCP client died while connected; restarting it");
    ip_.clear();
    setState(ConnState::ObtainingIp);
    startDhcp();
  }
}

void ConnectionManager::onAddressChange(bool added, const std::string& ip) {
  if (added && state_ == ConnState::ObtainingIp) onLease(ip);
  else if (!added && state_ == ConnState::Connected && ip == ip_) onLeaseLost("address removed");
}

// ---------------------------------------------------------------- connected

void ConnectionManager::pollSignal() {
  if (state_ != ConnState::Connected) return;
  if (auto li = sup_.linkInfo(); li && li->wpaState == "COMPLETED") {
    rssi_ = li->rssi;
    frequency_ = li->frequency;
    if (!li->bssid.empty()) bssid_ = li->bssid;
    TimePoint now = timers_.now();
    if (rssi_ < cfg_.roamRssiThreshold &&
        (lastRoamScan_ == TimePoint{} || now - lastRoamScan_ >= kRoamScanMinInterval)) {
      LOGI("weak signal (%d dBm); scanning for alternatives", rssi_);
      lastRoamScan_ = now;
      issueScan();
    }
  }
  pollTimer_.start(cfg_.signalPollInterval, [this] { pollSignal(); });
}

void ConnectionManager::backgroundScan() {
  if (state_ != ConnState::Connected) return;
  issueScan();
  bgScanTimer_.start(cfg_.bgScanInterval, [this] { backgroundScan(); });
}

void ConnectionManager::evaluateRoam() {
  if (!current_ || !canAutoconnect()) return;
  auto cand = selectNetwork(scan_, store_.profiles(), blacklist_, timers_.now(), cfg_.selection,
                            current_->ssid);
  if (!cand) return;
  bool higherPriority = cand->profile.priority > current_->priority;
  bool muchStronger = rssi_ < cfg_.roamRssiThreshold &&
                      cand->bss.rssi >= rssi_ + cfg_.roamHysteresis &&
                      cand->profile.priority >= current_->priority;
  if (!higherPriority && !muchStronger) return;
  LOGI("switching from '%s' (%d dBm) to '%s' (%d dBm, priority %d)", current_->ssid.c_str(), rssi_,
       cand->profile.ssid.c_str(), cand->bss.rssi, cand->profile.priority);
  emit("switching", Json::object().set("from", current_->ssid).set("to", cand->profile.ssid));
  beginConnect(cand->profile);
}

// ---------------------------------------------------------------- commands

void ConnectionManager::requestScan(ScanCallback cb) {
  if (!sup_.available()) {
    cb(false);
    return;
  }
  scanWaiters_.push_back(std::move(cb));
  if (!issueScan()) onScanDone(false);
}

OpResult ConnectionManager::saveProfile(const Profile& p) {
  if (auto e = p.validate()) return OpResult::failure(*e);
  std::optional<Profile> old;
  if (const Profile* existing = store_.find(p.ssid)) old = *existing;
  store_.upsert(p);
  std::string err;
  if (!store_.save(&err)) {
    if (old) store_.upsert(*old);
    else store_.remove(p.ssid);
    return OpResult::failure("could not persist profile: " + err);
  }
  LOGI("saved network '%s' (%s)", p.ssid.c_str(), toString(p.security));
  blacklist_.clear(p.ssid);
  authFailures_.erase(p.ssid);
  if (current_ && current_->ssid == p.ssid) current_ = p;
  if (state_ == ConnState::Idle && canAutoconnect()) {
    idleStep_ = 0;
    startScan();
  }
  return OpResult::success();
}

OpResult ConnectionManager::forget(const std::string& ssid) {
  const Profile* existing = store_.find(ssid);
  if (!existing) return OpResult::failure("unknown network");
  Profile old = *existing;
  store_.remove(ssid);
  std::string err;
  if (!store_.save(&err)) {
    store_.upsert(old);
    return OpResult::failure("could not persist: " + err);
  }
  LOGI("forgot network '%s'", ssid.c_str());
  blacklist_.clear(ssid);
  authFailures_.erase(ssid);
  if (current_ && current_->ssid == ssid) {
    teardown();
    finishPendingConnect(OpResult::failure("network was forgotten"));
    setState(ConnState::Idle);
    if (canAutoconnect()) startScan();
  }
  return OpResult::success();
}

void ConnectionManager::connect(const std::string& ssid, ConnectCallback cb) {
  const Profile* p = store_.find(ssid);
  if (!p) {
    cb(OpResult::failure("unknown network; add it first"));
    return;
  }
  if (!sup_.available()) {
    cb(OpResult::failure("wpa_supplicant unavailable"));
    return;
  }
  userDisconnected_ = false;
  blacklist_.clear(ssid);
  authFailures_.erase(ssid);
  if (state_ == ConnState::Connected && current_ && current_->ssid == ssid) {
    cb(OpResult::success());
    return;
  }
  finishPendingConnect(OpResult::failure("superseded by a new connect request"));
  pendingConnect_ = std::move(cb);
  Profile copy = *p;
  beginConnect(copy);
}

void ConnectionManager::disconnect() {
  userDisconnected_ = true;
  retryTimer_.cancel();
  finishPendingConnect(OpResult::failure("cancelled by disconnect"));
  teardown();
  if (state_ != ConnState::Unavailable) setState(ConnState::Idle);
}

void ConnectionManager::reconnect() {
  userDisconnected_ = false;
  idleStep_ = 0;
  blacklist_.clearAll();
  authFailures_.clear();
  if (state_ == ConnState::Idle) startScan();
}

void ConnectionManager::setAutoconnect(bool on) {
  autoconnect_ = on;
  if (!on) {
    retryTimer_.cancel();
    return;
  }
  if (state_ == ConnState::Idle && canAutoconnect()) {
    idleStep_ = 0;
    startScan();
  }
}

// ---------------------------------------------------------------- JSON views

Json ConnectionManager::statusJson() const {
  Json j = Json::object();
  j.set("state", toString(state_));
  j.set("ssid", current_ ? current_->ssid : std::string());
  j.set("bssid", bssid_);
  j.set("ip", ip_);
  if (state_ == ConnState::Connected) {
    j.set("rssi", rssi_);
    j.set("frequency", frequency_);
  }
  j.set("autoconnect", autoconnect_);
  j.set("paused", userDisconnected_);
  j.set("last_error", lastError_);
  j.set("saved_networks", static_cast<long>(store_.profiles().size()));
  return j;
}

Json ConnectionManager::scanResultsJson() const {
  std::vector<ScanResult> sorted = scan_;
  std::sort(sorted.begin(), sorted.end(),
            [](const ScanResult& a, const ScanResult& b) { return a.rssi > b.rssi; });
  Json arr = Json::array();
  for (const auto& r : sorted) {
    const Profile* p = store_.find(r.ssid);
    arr.push(Json::object()
                 .set("ssid", r.ssid)
                 .set("bssid", r.bssid)
                 .set("frequency", r.frequency)
                 .set("rssi", r.rssi)
                 .set("security", toString(r.security))
                 .set("flags", r.flags)
                 .set("saved", p != nullptr));
  }
  return arr;
}

Json ConnectionManager::profilesJson() const {
  Json arr = Json::array();
  TimePoint now = timers_.now();
  for (const auto& p : store_.profiles()) {
    std::string status = "ok";
    if (current_ && current_->ssid == p.ssid) status = "current";
    else if (blacklist_.suspended(p.ssid)) status = "credentials_rejected";
    else if (blacklist_.blocked(p.ssid, now)) status = "backoff";
    // Secrets are deliberately never included.
    arr.push(Json::object()
                 .set("ssid", p.ssid)
                 .set("security", toString(p.security))
                 .set("priority", p.priority)
                 .set("hidden", p.hidden)
                 .set("autoconnect", p.autoconnect)
                 .set("failures", blacklist_.failures(p.ssid))
                 .set("status", status));
  }
  return arr;
}

}  // namespace wifimgr
