// wifimgrd: Wi-Fi station manager daemon for embedded Linux.
//
// Wiring: wpa_supplicant control socket <-> ConnectionManager <-> control
// socket (wifictl), plus the DHCP client process and rtnetlink, all driven
// by one epoll loop.

#include <getopt.h>
#include <signal.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>

#include "wifimgr/commands.hpp"
#include "wifimgr/config.hpp"
#include "wifimgr/connection_manager.hpp"
#include "wifimgr/control_server.hpp"
#include "wifimgr/dhcp_client.hpp"
#include "wifimgr/event_loop.hpp"
#include "wifimgr/log.hpp"
#include "wifimgr/netlink_monitor.hpp"
#include "wifimgr/profile_store.hpp"
#include "wifimgr/supplicant.hpp"
#include "wifimgr/version.hpp"

using namespace wifimgr;

namespace {

// IDhcpClient that does nothing (dhcp_command empty): addressing is done by
// someone else and detected through netlink.
class NoDhcp : public IDhcpClient {
 public:
  void setExitHandler(ExitHandler) override {}
  bool start() override { return true; }
  void stop() override {}
  bool running() const override { return true; }
};

void usage(FILE* out) {
  std::fprintf(out,
               "Usage: wifimgrd [options]\n"
               "  -c, --config FILE     configuration file (default /etc/wifimgr/wifimgr.conf)\n"
               "  -i, --interface IF    wireless interface (overrides config)\n"
               "  -s, --socket PATH     control socket path (overrides config)\n"
               "  -d, --debug           debug logging\n"
               "      --syslog          log to syslog instead of stderr\n"
               "  -t, --check-config    validate configuration and exit\n"
               "  -V, --version         print version and exit\n"
               "  -h, --help            this help\n");
}

bool ensureDir(const std::string& dir) {
  if (mkdir(dir.c_str(), 0750) == 0 || errno == EEXIST) return true;
  LOGE("cannot create %s: %s", dir.c_str(), std::strerror(errno));
  return false;
}

std::string parentDir(const std::string& path) {
  auto slash = path.rfind('/');
  return slash == std::string::npos || slash == 0 ? "/" : path.substr(0, slash);
}

}  // namespace

int main(int argc, char** argv) {
  std::string configPath = "/etc/wifimgr/wifimgr.conf";
  bool configExplicit = false;
  std::string ifaceOverride, socketOverride;
  bool debug = false, syslogOpt = false, checkOnly = false;

  static const option opts[] = {
      {"config", required_argument, nullptr, 'c'},  {"interface", required_argument, nullptr, 'i'},
      {"socket", required_argument, nullptr, 's'},  {"debug", no_argument, nullptr, 'd'},
      {"syslog", no_argument, nullptr, 'S'},        {"check-config", no_argument, nullptr, 't'},
      {"version", no_argument, nullptr, 'V'},       {"help", no_argument, nullptr, 'h'},
      {nullptr, 0, nullptr, 0}};
  int c;
  while ((c = getopt_long(argc, argv, "c:i:s:dtVh", opts, nullptr)) != -1) {
    switch (c) {
      case 'c': configPath = optarg; configExplicit = true; break;
      case 'i': ifaceOverride = optarg; break;
      case 's': socketOverride = optarg; break;
      case 'd': debug = true; break;
      case 'S': syslogOpt = true; break;
      case 't': checkOnly = true; break;
      case 'V': std::printf("wifimgrd %s\n", WIFIMGR_VERSION); return 0;
      case 'h': usage(stdout); return 0;
      default: usage(stderr); return 2;
    }
  }

  DaemonConfig cfg;
  std::vector<std::string> errors;
  if (configExplicit || access(configPath.c_str(), F_OK) == 0) {
    if (!loadConfigFile(configPath, cfg, &errors)) {
      for (const auto& e : errors) std::fprintf(stderr, "wifimgrd: %s\n", e.c_str());
      return 1;
    }
  }
  if (!ifaceOverride.empty()) {
    std::string err;
    if (!applyConfigValue(cfg, "interface", ifaceOverride, &err)) {
      std::fprintf(stderr, "wifimgrd: --interface: %s\n", err.c_str());
      return 2;
    }
  }
  if (!socketOverride.empty()) cfg.controlSocket = socketOverride;
  if (checkOnly) {
    std::printf("configuration OK\n");
    return 0;
  }

  setLogLevel(debug ? LogLevel::Debug : cfg.logLevel);
  setLogSyslog(syslogOpt || cfg.syslog);
  LOGI("wifimgrd %s starting on %s", WIFIMGR_VERSION, cfg.interface.c_str());

  if (!ensureDir(cfg.wpaClientDir) || !ensureDir(parentDir(cfg.controlSocket))) return 1;

  EventLoop loop;
  loop.watchSignals({SIGTERM, SIGINT}, [&](const signalfd_siginfo& si) {
    LOGI("received signal %u, shutting down", si.ssi_signo);
    loop.stop(0);
  });
  signal(SIGPIPE, SIG_IGN);

  ProfileStore store(cfg.profiles);
  std::string err;
  if (!store.load(&err)) {
    LOGE("%s", err.c_str());
    return 1;
  }
  LOGI("loaded %zu saved network(s) from %s", store.profiles().size(), cfg.profiles.c_str());

  WpaSupplicant supplicant(loop, cfg.interface, cfg.wpaCtrlDir, cfg.wpaClientDir);
  std::unique_ptr<IDhcpClient> dhcp;
  if (cfg.dhcpCommand.empty()) dhcp = std::make_unique<NoDhcp>();
  else dhcp = std::make_unique<ProcessDhcpClient>(loop, cfg.dhcpCommand, cfg.interface);

  ConnectionManager mgr(supplicant, *dhcp, store, loop, cfg.manager);
  CommandHandler commands(mgr, nullptr, geteuid());
  ControlServer server(loop, cfg.controlSocket, cfg.controlSocketMode,
                       [&](const ControlServer::Request& r, ControlServer::Responder resp) {
                         commands.handle(r, std::move(resp));
                       });
  commands.setServer(&server);
  if (!server.start(&err)) {
    LOGE("control socket: %s", err.c_str());
    return 1;
  }
  mgr.setEventSink([&](const std::string& name, const Json& data) {
    server.broadcast(Json::object().set("event", name).set("data", data));
  });

  NetlinkMonitor netlink(loop, cfg.interface);
  if (cfg.netlink) {
    netlink.setAddressHandler([&](bool added, const std::string& ip) { mgr.onAddressChange(added, ip); });
    netlink.setLinkHandler([&](bool exists, bool up) {
      LOGI("interface %s %s", cfg.interface.c_str(), !exists ? "removed" : up ? "up" : "down");
    });
    if (!netlink.start()) LOGW("netlink unavailable; relying on DHCP hook events only");
  }

  mgr.start();
  supplicant.start();
  int rc = loop.run();
  LOGI("stopped");
  return rc;
}
