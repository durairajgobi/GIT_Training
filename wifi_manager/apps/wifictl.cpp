// wifictl: command-line client for wifimgrd.

#include <termios.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "wifimgr/control_client.hpp"
#include "wifimgr/json.hpp"
#include "wifimgr/version.hpp"

using namespace wifimgr;

namespace {

const char* kDefaultSocket = "/run/wifimgr/wifimgr.sock";

void usage(FILE* out) {
  std::fprintf(out,
      "Usage: wifictl [-s SOCKET] [--json] <command> [args]\n"
      "\n"
      "Commands:\n"
      "  status                       connection state, SSID, IP, signal\n"
      "  scan                         scan and list networks in range\n"
      "  results                      list results of the last scan\n"
      "  networks                     list saved networks (secrets are never shown)\n"
      "  add <ssid> [options]         save or update a network\n"
      "  connect <ssid> [options]     (save and) connect; waits for an IP address\n"
      "  forget <ssid>                delete a saved network\n"
      "  disconnect                   disconnect and pause autoconnect\n"
      "  reconnect                    resume autoconnect, clear failure backoff\n"
      "  autoconnect on|off\n"
      "  monitor                      print events as they happen\n"
      "  dhcp-event <event> [ip]      used by the DHCP client hook script\n"
      "  version\n"
      "\n"
      "Options for add/connect:\n"
      "  --security open|wpa-psk|sae|wpa-psk-sae|wpa-eap\n"
      "  --psk PASS          passphrase ('-' reads it from stdin, without echo on a tty)\n"
      "  --priority N        higher wins (0..1000)\n"
      "  --hidden            network does not broadcast its SSID\n"
      "  --no-autoconnect    only join on explicit connect\n"
      "  --eap PEAP|TTLS --identity ID --anonymous-identity ID --password PASS|-\n"
      "  --phase2 STR --ca-cert FILE\n");
}

std::string readSecret(const char* prompt) {
  bool tty = isatty(STDIN_FILENO);
  termios old{};
  if (tty) {
    std::fprintf(stderr, "%s", prompt);
    tcgetattr(STDIN_FILENO, &old);
    termios noecho = old;
    noecho.c_lflag &= ~static_cast<tcflag_t>(ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &noecho);
  }
  std::string s;
  std::getline(std::cin, s);
  if (tty) {
    tcsetattr(STDIN_FILENO, TCSANOW, &old);
    std::fprintf(stderr, "\n");
  }
  if (!s.empty() && s.back() == '\r') s.pop_back();
  return s;
}

// Translates --flags into the protocol's "key value" pairs.
bool translateOptions(const std::vector<std::string>& in, size_t first,
                      std::vector<std::string>* out, std::string* err) {
  for (size_t i = first; i < in.size(); ++i) {
    const std::string& a = in[i];
    auto value = [&](std::string* v) {
      if (i + 1 >= in.size()) {
        *err = a + " needs a value";
        return false;
      }
      *v = in[++i];
      return true;
    };
    std::string v;
    if (a == "--hidden") {
      out->insert(out->end(), {"hidden", "on"});
    } else if (a == "--no-autoconnect") {
      out->insert(out->end(), {"autoconnect", "off"});
    } else if (a == "--psk" || a == "--password") {
      if (!value(&v)) return false;
      if (v == "-") v = readSecret(a == "--psk" ? "Passphrase: " : "Password: ");
      out->insert(out->end(), {a.substr(2), v});
    } else if (a == "--security" || a == "--priority" || a == "--eap" || a == "--identity" ||
               a == "--phase2") {
      if (!value(&v)) return false;
      out->insert(out->end(), {a.substr(2), v});
    } else if (a == "--anonymous-identity") {
      if (!value(&v)) return false;
      out->insert(out->end(), {"anonymous_identity", v});
    } else if (a == "--ca-cert") {
      if (!value(&v)) return false;
      out->insert(out->end(), {"ca_cert", v});
    } else {
      *err = "unknown option " + a;
      return false;
    }
  }
  return true;
}

std::string pad(const std::string& s, size_t w) {
  return s.size() >= w ? s + " " : s + std::string(w - s.size(), ' ');
}

std::string printable(const std::string& s) {
  if (s.empty()) return "<hidden>";
  std::string out;
  for (unsigned char c : s) {
    if (c < 0x20 || c == 0x7f) {
      char buf[8];
      std::snprintf(buf, sizeof buf, "\\x%02x", c);
      out += buf;
    } else {
      out.push_back(static_cast<char>(c));
    }
  }
  return out;
}

void printStatus(const Json& s) {
  std::printf("State:       %s%s\n", s.getString("state").c_str(),
              s.getBool("paused") ? " (paused by user)" : "");
  if (!s.getString("ssid").empty()) std::printf("SSID:        %s\n", printable(s.getString("ssid")).c_str());
  if (!s.getString("bssid").empty()) std::printf("BSSID:       %s\n", s.getString("bssid").c_str());
  if (!s.getString("ip").empty()) std::printf("IP address:  %s\n", s.getString("ip").c_str());
  if (s.get("rssi")) std::printf("Signal:      %ld dBm\n", s.getInt("rssi"));
  if (s.getInt("frequency")) std::printf("Frequency:   %ld MHz\n", s.getInt("frequency"));
  std::printf("Autoconnect: %s\n", s.getBool("autoconnect") ? "on" : "off");
  std::printf("Saved:       %ld network(s)\n", s.getInt("saved_networks"));
  if (!s.getString("last_error").empty())
    std::printf("Last error:  %s\n", printable(s.getString("last_error")).c_str());
}

void printResults(const Json& arr) {
  std::printf("%s%s%s%s%s%s\n", pad("SSID", 33).c_str(), pad("BSSID", 18).c_str(),
              pad("FREQ", 6).c_str(), pad("dBm", 5).c_str(), pad("SECURITY", 12).c_str(), "SAVED");
  for (const auto& r : arr.items()) {
    std::printf("%s%s%s%s%s%s\n", pad(printable(r.getString("ssid")), 33).c_str(),
                pad(r.getString("bssid"), 18).c_str(),
                pad(std::to_string(r.getInt("frequency")), 6).c_str(),
                pad(std::to_string(r.getInt("rssi")), 5).c_str(),
                pad(r.getString("security"), 12).c_str(), r.getBool("saved") ? "*" : "");
  }
}

void printNetworks(const Json& arr) {
  std::printf("%s%s%s%s%s%s\n", pad("SSID", 33).c_str(), pad("SECURITY", 12).c_str(),
              pad("PRIO", 5).c_str(), pad("HIDDEN", 7).c_str(), pad("AUTO", 5).c_str(), "STATUS");
  for (const auto& n : arr.items()) {
    std::printf("%s%s%s%s%s%s\n", pad(printable(n.getString("ssid")), 33).c_str(),
                pad(n.getString("security"), 12).c_str(),
                pad(std::to_string(n.getInt("priority")), 5).c_str(),
                pad(n.getBool("hidden") ? "yes" : "no", 7).c_str(),
                pad(n.getBool("autoconnect") ? "yes" : "no", 5).c_str(),
                n.getString("status").c_str());
  }
}

}  // namespace

int main(int argc, char** argv) {
  std::string socketPath = getenv("WIFIMGR_SOCKET") ? getenv("WIFIMGR_SOCKET") : kDefaultSocket;
  bool json = false;
  std::vector<std::string> args;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (args.empty() && (a == "-s" || a == "--socket") && i + 1 < argc) socketPath = argv[++i];
    else if (args.empty() && a == "--json") json = true;
    else if (args.empty() && (a == "-h" || a == "--help")) {
      usage(stdout);
      return 0;
    } else args.push_back(a);
  }
  if (args.empty()) {
    usage(stderr);
    return 2;
  }

  std::vector<std::string> request = {args[0]};
  if ((args[0] == "add" || args[0] == "connect") && args.size() >= 2) {
    request.push_back(args[1]);
    std::string err;
    if (!translateOptions(args, 2, &request, &err)) {
      std::fprintf(stderr, "wifictl: %s\n", err.c_str());
      return 2;
    }
  } else {
    request.insert(request.end(), args.begin() + 1, args.end());
  }

  ControlClient client;
  std::string err;
  if (!client.connect(socketPath, &err)) {
    std::fprintf(stderr, "wifictl: cannot reach wifimgrd: %s\n", err.c_str());
    return 3;
  }
  if (!client.send(request)) {
    std::fprintf(stderr, "wifictl: send failed\n");
    return 3;
  }
  // connect waits for association + DHCP; allow for both timeouts.
  int timeout = (args[0] == "connect" || args[0] == "scan") ? 120000 : 10000;
  auto line = client.readLine(timeout);
  if (!line) {
    std::fprintf(stderr, "wifictl: no reply from wifimgrd\n");
    return 3;
  }
  auto reply = Json::parse(*line);
  if (!reply) {
    std::fprintf(stderr, "wifictl: malformed reply\n");
    return 3;
  }
  if (json) std::printf("%s\n", line->c_str());
  std::fflush(stdout);  // `monitor` output is usually piped; do not sit in a buffer
  if (!reply->getBool("ok")) {
    if (!json) std::fprintf(stderr, "wifictl: %s\n", reply->getString("error").c_str());
    return 1;
  }

  const std::string& cmd = args[0];
  if (cmd == "monitor") {
    if (!json) std::printf("monitoring events (Ctrl-C to stop)\n");
    std::fflush(stdout);
    for (;;) {
      auto ev = client.readLine(-1);
      if (!ev) return 0;
      if (json) {
        std::printf("%s\n", ev->c_str());
      } else if (auto e = Json::parse(*ev)) {
        std::printf("%-20s %s\n", e->getString("event").c_str(),
                    e->get("data") ? e->get("data")->dump().c_str() : "");
      }
      std::fflush(stdout);
    }
  }
  if (json) return 0;
  if (cmd == "status") printStatus(*reply->get("status"));
  else if (cmd == "scan" || cmd == "results") printResults(*reply->get("results"));
  else if (cmd == "networks") printNetworks(*reply->get("networks"));
  else if (cmd == "connect") {
    const Json* s = reply->get("status");
    std::printf("Connected to %s, IP %s\n", printable(s->getString("ssid")).c_str(),
                s->getString("ip").c_str());
  } else if (cmd == "version") std::printf("wifimgrd %s, wifictl %s\n",
                                           reply->getString("version").c_str(), WIFIMGR_VERSION);
  else if (cmd != "dhcp-event") std::printf("OK\n");
  return 0;
}
