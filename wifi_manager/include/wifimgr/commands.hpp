// Maps control-protocol requests onto the ConnectionManager.
//
//   ping | version | status | results | networks | monitor
//   scan
//   add <ssid> [key value]...        key: security psk priority hidden autoconnect
//   connect <ssid> [key value]...         eap identity anonymous_identity password
//   forget <ssid>                         phase2 ca_cert
//   disconnect | reconnect | autoconnect on|off
//   dhcp-event <bound|renew|deconfig|leasefail|nak> [ip]   (from the DHCP hook)
#pragma once

#include <sys/types.h>

#include <optional>
#include <string>
#include <vector>

#include "wifimgr/connection_manager.hpp"
#include "wifimgr/control_server.hpp"

namespace wifimgr {

class CommandHandler {
 public:
  CommandHandler(ConnectionManager& mgr, ControlServer* server, uid_t adminUid)
      : mgr_(mgr), server_(server), adminUid_(adminUid) {}

  void setServer(ControlServer* s) { server_ = s; }
  void handle(const ControlServer::Request& req, ControlServer::Responder respond);

  // Builds/updates a profile from "key value" pairs. Exposed for tests.
  static std::optional<std::string> applyOptions(Profile& p, const std::vector<std::string>& args,
                                                 size_t first, bool* securityGiven);

 private:
  bool authorized(uid_t uid) const { return uid == 0 || uid == adminUid_; }
  OpResult upsertFromArgs(const std::vector<std::string>& args);

  ConnectionManager& mgr_;
  ControlServer* server_;
  uid_t adminUid_;
};

}  // namespace wifimgr
