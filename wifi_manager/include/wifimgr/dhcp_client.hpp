// DHCP client process supervision. The daemon runs an external client
// (busybox udhcpc by default) whose hook script reports lease events back
// over the control socket (`wifictl dhcp-event bound <ip>`).
#pragma once

#include <sys/types.h>

#include <functional>
#include <set>
#include <string>


namespace wifimgr {

class EventLoop;

class IDhcpClient {
 public:
  // Called when the client exits without having been asked to stop.
  using ExitHandler = std::function<void(int exitCode)>;
  virtual ~IDhcpClient() = default;
  virtual void setExitHandler(ExitHandler h) = 0;
  virtual bool start() = 0;
  virtual void stop() = 0;
  virtual bool running() const = 0;
};

class ProcessDhcpClient : public IDhcpClient {
 public:
  // `commandTemplate` is run with /bin/sh -c after replacing {iface}.
  ProcessDhcpClient(EventLoop& loop, std::string commandTemplate, std::string iface);
  ~ProcessDhcpClient() override;

  void setExitHandler(ExitHandler h) override { onExit_ = std::move(h); }
  bool start() override;
  void stop() override;
  bool running() const override { return pid_ > 0; }

 private:
  void onChildExit(pid_t pid, int status);

  EventLoop& loop_;
  std::string command_;
  std::string iface_;
  pid_t pid_ = -1;
  std::set<pid_t> stopping_;  // signalled, not yet reaped
  ExitHandler onExit_;
};

}  // namespace wifimgr
