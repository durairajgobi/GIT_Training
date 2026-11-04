// Control socket: UNIX stream socket, one request per line, one JSON object
// per line in reply. Clients that issue `monitor` additionally receive
// {"event": ...} lines for every manager event.
#pragma once

#include <sys/types.h>

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "wifimgr/json.hpp"

namespace wifimgr {

class EventLoop;

class ControlServer {
 public:
  struct Request {
    uint64_t client = 0;
    uid_t uid = static_cast<uid_t>(-1);  // from SO_PEERCRED
    std::vector<std::string> args;
  };
  // May be called later (asynchronous commands). Safe to call after the
  // client went away or the server was destroyed: the reply is dropped.
  using Responder = std::function<void(const Json& reply)>;
  using Handler = std::function<void(const Request& req, Responder respond)>;

  static constexpr size_t kMaxLine = 4096;
  static constexpr size_t kMaxClients = 16;
  static constexpr size_t kMaxOutput = 1 << 20;

  ControlServer(EventLoop& loop, std::string path, mode_t mode, Handler handler);
  ~ControlServer();

  bool start(std::string* err);
  void subscribe(uint64_t client);
  void broadcast(const Json& event);

 private:
  struct Client {
    int fd = -1;
    uid_t uid = static_cast<uid_t>(-1);
    std::string in;
    std::string out;
    bool monitor = false;
  };
  void onAccept();
  void onClient(uint64_t id, uint32_t events);
  void send(uint64_t id, const std::string& line);
  void flush(uint64_t id);
  void drop(uint64_t id);

  EventLoop& loop_;
  std::string path_;
  mode_t mode_;
  Handler handler_;
  int listenFd_ = -1;
  uint64_t nextId_ = 1;
  std::map<uint64_t, Client> clients_;
  std::shared_ptr<ControlServer*> self_;  // weak handle for responders
};

}  // namespace wifimgr
