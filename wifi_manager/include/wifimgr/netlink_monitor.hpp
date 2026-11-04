// rtnetlink listener: reports IPv4 address and link changes on the managed
// interface. Gives the manager an independent signal that an address was
// configured (static setups, other DHCP clients) or removed.
#pragma once

#include <functional>
#include <string>

namespace wifimgr {

class EventLoop;

class NetlinkMonitor {
 public:
  using AddressHandler = std::function<void(bool added, const std::string& ip)>;
  using LinkHandler = std::function<void(bool exists, bool up)>;

  NetlinkMonitor(EventLoop& loop, std::string iface);
  ~NetlinkMonitor();

  bool start();
  void setAddressHandler(AddressHandler h) { onAddr_ = std::move(h); }
  void setLinkHandler(LinkHandler h) { onLink_ = std::move(h); }

 private:
  void onReadable();

  EventLoop& loop_;
  std::string iface_;
  int fd_ = -1;
  AddressHandler onAddr_;
  LinkHandler onLink_;
};

}  // namespace wifimgr
