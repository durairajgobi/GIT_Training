#include "wifimgr/commands.hpp"

#include <set>

#include "wifimgr/log.hpp"
#include "wifimgr/strutil.hpp"
#include "wifimgr/version.hpp"

namespace wifimgr {
namespace {

Json ok() { return Json::object().set("ok", true); }
Json fail(const std::string& e) { return Json::object().set("ok", false).set("error", e); }
Json fromResult(const OpResult& r) { return r.ok ? ok() : fail(r.error); }

const std::set<std::string> kReadOnly = {"ping", "version", "status", "results",
                                         "networks", "monitor", "scan"};

}  // namespace

std::optional<std::string> CommandHandler::applyOptions(Profile& p,
                                                        const std::vector<std::string>& args,
                                                        size_t first, bool* securityGiven) {
  *securityGiven = false;
  if ((args.size() - first) % 2 != 0) return "options must be key value pairs";
  for (size_t i = first; i + 1 < args.size(); i += 2) {
    const std::string& k = args[i];
    const std::string& v = args[i + 1];
    if (k == "security") {
      auto s = securityFromString(v);
      if (!s) return "unknown security '" + v + "'";
      p.security = *s;
      *securityGiven = true;
    } else if (k == "psk") p.psk = v;
    else if (k == "eap") p.eap = v;
    else if (k == "identity") p.identity = v;
    else if (k == "anonymous_identity") p.anonymousIdentity = v;
    else if (k == "password") p.password = v;
    else if (k == "phase2") p.phase2 = v;
    else if (k == "ca_cert") p.caCert = v;
    else if (k == "priority") {
      auto n = parseInt(v);
      if (!n || *n < 0 || *n > 1000) return "priority must be 0..1000";
      p.priority = static_cast<int>(*n);
    } else if (k == "hidden") {
      if (!parseBool(v, &p.hidden)) return "hidden must be on/off";
    } else if (k == "autoconnect") {
      if (!parseBool(v, &p.autoconnect)) return "autoconnect must be on/off";
    } else {
      return "unknown option '" + k + "'";
    }
  }
  return std::nullopt;
}

OpResult CommandHandler::upsertFromArgs(const std::vector<std::string>& args) {
  if (args.size() < 2) return OpResult::failure("usage: " + args[0] + " <ssid> [key value]...");
  // Start from the stored profile so `add X priority 5` only changes priority.
  std::optional<Profile> stored = mgr_.findProfile(args[1]);
  bool found = stored.has_value();
  Profile p = found ? *stored : Profile{};
  p.ssid = args[1];
  bool securityGiven = false;
  if (auto e = applyOptions(p, args, 2, &securityGiven)) return OpResult::failure(*e);
  if (!found && !securityGiven) {
    if (!p.identity.empty()) p.security = Security::WpaEap;
    else p.security = p.psk.empty() ? Security::Open : Security::WpaPsk;
  }
  return mgr_.saveProfile(p);
}

void CommandHandler::handle(const ControlServer::Request& req, ControlServer::Responder respond) {
  const auto& a = req.args;
  if (a.empty()) return respond(fail("empty request"));
  const std::string& cmd = a[0];
  if (!kReadOnly.count(cmd) && !authorized(req.uid)) {
    LOGW("control: uid %u not allowed to run '%s'", static_cast<unsigned>(req.uid), cmd.c_str());
    return respond(fail("permission denied"));
  }

  if (cmd == "ping") return respond(ok().set("pong", true));
  if (cmd == "version") return respond(ok().set("version", WIFIMGR_VERSION));
  if (cmd == "status") return respond(ok().set("status", mgr_.statusJson()));
  if (cmd == "results") return respond(ok().set("results", mgr_.scanResultsJson()));
  if (cmd == "networks") return respond(ok().set("networks", mgr_.profilesJson()));
  if (cmd == "monitor") {
    if (server_) server_->subscribe(req.client);
    return respond(ok().set("monitoring", true));
  }
  if (cmd == "scan") {
    mgr_.requestScan([this, respond](bool success) {
      if (!success) return respond(fail("scan failed"));
      respond(ok().set("results", mgr_.scanResultsJson()));
    });
    return;
  }
  if (cmd == "add") return respond(fromResult(upsertFromArgs(a)));
  if (cmd == "connect") {
    if (a.size() < 2) return respond(fail("usage: connect <ssid> [key value]..."));
    if (a.size() > 2) {
      OpResult r = upsertFromArgs(a);
      if (!r.ok) return respond(fail(r.error));
    }
    mgr_.connect(a[1], [this, respond](const OpResult& r) {
      if (!r.ok) return respond(fail(r.error));
      respond(ok().set("status", mgr_.statusJson()));
    });
    return;
  }
  if (cmd == "forget") {
    if (a.size() != 2) return respond(fail("usage: forget <ssid>"));
    return respond(fromResult(mgr_.forget(a[1])));
  }
  if (cmd == "disconnect") {
    mgr_.disconnect();
    return respond(ok());
  }
  if (cmd == "reconnect") {
    mgr_.reconnect();
    return respond(ok());
  }
  if (cmd == "autoconnect") {
    bool on = false;
    if (a.size() != 2 || !parseBool(a[1], &on)) return respond(fail("usage: autoconnect on|off"));
    mgr_.setAutoconnect(on);
    return respond(ok());
  }
  if (cmd == "dhcp-event") {
    if (a.size() < 2) return respond(fail("usage: dhcp-event <event> [ip]"));
    mgr_.onDhcpEvent(a[1], a.size() > 2 ? a[2] : std::string());
    return respond(ok());
  }
  respond(fail("unknown command '" + cmd + "'"));
}

}  // namespace wifimgr
