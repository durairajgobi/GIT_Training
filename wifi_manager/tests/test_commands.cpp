#include "fakes.hpp"
#include "test.hpp"
#include "wifimgr/commands.hpp"
#include "wifimgr/config.hpp"

using namespace wifimgr;

namespace {

struct CmdRig {
  fake::Timers timers;
  fake::Supplicant sup;
  fake::Dhcp dhcp;
  ProfileStore store{fake::tempPath()};
  ConnectionManager mgr{sup, dhcp, store, timers, ManagerConfig{}};
  CommandHandler handler{mgr, nullptr, 1000};

  CmdRig() { mgr.start(); }

  Json run(std::vector<std::string> args, uid_t uid = 0) {
    Json out;
    ControlServer::Request req;
    req.uid = uid;
    req.args = std::move(args);
    handler.handle(req, [&](const Json& j) { out = j; });
    return out;
  }
};

}  // namespace

TEST(cmd_add_infers_security_and_updates_partially) {
  CmdRig r;
  CHECK(r.run({"add", "Cafe"}).getBool("ok"));
  CHECK(r.store.find("Cafe")->security == Security::Open);

  CHECK(r.run({"add", "Home", "psk", "password123", "priority", "4", "hidden", "on"}).getBool("ok"));
  const Profile* h = r.store.find("Home");
  REQUIRE(h);
  CHECK(h->security == Security::WpaPsk);
  CHECK(h->hidden);
  CHECK_EQ(h->priority, 4);

  CHECK(r.run({"add", "Home", "priority", "9"}).getBool("ok"));
  CHECK_EQ(r.store.find("Home")->psk, std::string("password123"));  // kept
  CHECK_EQ(r.store.find("Home")->priority, 9);

  CHECK(r.run({"add", "Corp", "identity", "me", "password", "pw"}).getBool("ok"));
  CHECK(r.store.find("Corp")->security == Security::WpaEap);
}

TEST(cmd_add_reports_errors) {
  CmdRig r;
  CHECK_EQ(r.run({"add", "X", "psk"}).getString("error"), std::string("options must be key value pairs"));
  CHECK(r.run({"add", "X", "colour", "red"}).getString("error").find("unknown option") != std::string::npos);
  CHECK(r.run({"add", "X", "psk", "short"}).getString("error").find("passphrase") != std::string::npos);
  CHECK(!r.run({"add"}).getBool("ok"));
}

TEST(cmd_permissions) {
  CmdRig r;
  CHECK(r.run({"status"}, 4242).getBool("ok"));
  CHECK(r.run({"networks"}, 4242).getBool("ok"));
  CHECK_EQ(r.run({"add", "X"}, 4242).getString("error"), std::string("permission denied"));
  CHECK_EQ(r.run({"dhcp-event", "bound", "1.2.3.4"}, 4242).getString("error"),
           std::string("permission denied"));
  CHECK(r.run({"add", "X"}, 1000).getBool("ok"));  // daemon's own uid
}

TEST(cmd_networks_hides_secrets) {
  CmdRig r;
  r.run({"add", "Home", "psk", "supersecret1"});
  std::string out = r.run({"networks"}).dump();
  CHECK(out.find("Home") != std::string::npos);
  CHECK(out.find("supersecret1") == std::string::npos);
}

TEST(cmd_connect_is_asynchronous) {
  CmdRig r;
  Json reply;
  ControlServer::Request req;
  req.uid = 0;
  req.args = {"connect", "Home", "psk", "password123"};
  r.handler.handle(req, [&](const Json& j) { reply = j; });
  CHECK(reply.isNull());  // pending
  r.sup.connected();
  r.mgr.onDhcpEvent("bound", "10.9.8.7");
  CHECK(reply.getBool("ok"));
  CHECK_EQ(reply.get("status")->getString("ip"), std::string("10.9.8.7"));
}

TEST(cmd_misc) {
  CmdRig r;
  CHECK(r.run({"ping"}).getBool("pong"));
  CHECK(r.run({"autoconnect", "off"}).getBool("ok"));
  CHECK(!r.run({"autoconnect", "sideways"}).getBool("ok"));
  CHECK(!r.run({"forget", "nope"}).getBool("ok"));
  CHECK(!r.run({"frobnicate"}).getBool("ok"));
  CHECK(!r.run({}).getBool("ok"));
}

TEST(config_parsing) {
  DaemonConfig cfg;
  std::string err;
  CHECK(applyConfigValue(cfg, "interface", "wlp2s0", &err));
  CHECK(!applyConfigValue(cfg, "interface", "wlan0; rm -rf /", &err));
  CHECK(!applyConfigValue(cfg, "interface", "averyveryverylongname", &err));
  CHECK(applyConfigValue(cfg, "connect_timeout", "40", &err));
  CHECK(cfg.manager.connectTimeout == Millis(40000));
  CHECK(!applyConfigValue(cfg, "connect_timeout", "0", &err));
  CHECK(applyConfigValue(cfg, "control_socket_mode", "0600", &err));
  CHECK_EQ(cfg.controlSocketMode, static_cast<mode_t>(0600));
  CHECK(!applyConfigValue(cfg, "control_socket_mode", "999", &err));
  CHECK(!applyConfigValue(cfg, "profiles", "relative/path", &err));
  CHECK(!applyConfigValue(cfg, "no_such_key", "1", &err));
  CHECK(applyConfigValue(cfg, "min_rssi", "-80", &err));
  CHECK_EQ(cfg.manager.selection.minRssi, -80);
}
