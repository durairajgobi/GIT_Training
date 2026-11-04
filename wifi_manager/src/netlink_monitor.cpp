#include "wifimgr/netlink_monitor.hpp"

#include <arpa/inet.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <net/if.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

#include "wifimgr/event_loop.hpp"
#include "wifimgr/log.hpp"

namespace wifimgr {

NetlinkMonitor::NetlinkMonitor(EventLoop& loop, std::string iface)
    : loop_(loop), iface_(std::move(iface)) {}

NetlinkMonitor::~NetlinkMonitor() {
  if (fd_ >= 0) {
    loop_.removeFd(fd_);
    close(fd_);
  }
}

bool NetlinkMonitor::start() {
  fd_ = socket(AF_NETLINK, SOCK_RAW | SOCK_NONBLOCK | SOCK_CLOEXEC, NETLINK_ROUTE);
  if (fd_ < 0) {
    LOGW("netlink socket: %s", std::strerror(errno));
    return false;
  }
  sockaddr_nl sa{};
  sa.nl_family = AF_NETLINK;
  sa.nl_groups = RTMGRP_LINK | RTMGRP_IPV4_IFADDR;
  if (bind(fd_, reinterpret_cast<sockaddr*>(&sa), sizeof sa) < 0) {
    LOGW("netlink bind: %s", std::strerror(errno));
    close(fd_);
    fd_ = -1;
    return false;
  }
  return loop_.addFd(fd_, EPOLLIN, [this](uint32_t) { onReadable(); });
}

void NetlinkMonitor::onReadable() {
  alignas(nlmsghdr) char buf[8192];
  for (;;) {
    ssize_t n = recv(fd_, buf, sizeof buf, 0);
    if (n < 0) {
      if (errno == ENOBUFS) {
        LOGW("netlink: receive buffer overrun, events lost");
        continue;
      }
      return;  // EAGAIN
    }
    // Interface index may change when a USB adapter is re-plugged.
    unsigned ifindex = if_nametoindex(iface_.c_str());
    for (auto* nh = reinterpret_cast<nlmsghdr*>(buf); NLMSG_OK(nh, static_cast<unsigned>(n));
         nh = NLMSG_NEXT(nh, n)) {
      if (nh->nlmsg_type == RTM_NEWADDR || nh->nlmsg_type == RTM_DELADDR) {
        auto* ifa = static_cast<ifaddrmsg*>(NLMSG_DATA(nh));
        if (ifa->ifa_family != AF_INET || ifa->ifa_index != ifindex) continue;
        int len = static_cast<int>(IFA_PAYLOAD(nh));
        for (auto* rta = IFA_RTA(ifa); RTA_OK(rta, len); rta = RTA_NEXT(rta, len)) {
          if (rta->rta_type != IFA_LOCAL && rta->rta_type != IFA_ADDRESS) continue;
          char ip[INET_ADDRSTRLEN];
          inet_ntop(AF_INET, RTA_DATA(rta), ip, sizeof ip);
          bool added = nh->nlmsg_type == RTM_NEWADDR;
          LOGD("netlink: %s %s on %s", added ? "added" : "removed", ip, iface_.c_str());
          if (onAddr_) onAddr_(added, ip);
          break;
        }
      } else if (nh->nlmsg_type == RTM_NEWLINK || nh->nlmsg_type == RTM_DELLINK) {
        auto* ifi = static_cast<ifinfomsg*>(NLMSG_DATA(nh));
        // Match by name: the index is gone for DELLINK.
        int len = static_cast<int>(IFLA_PAYLOAD(nh));
        bool ours = false;
        for (auto* rta = IFLA_RTA(ifi); RTA_OK(rta, len); rta = RTA_NEXT(rta, len)) {
          if (rta->rta_type == IFLA_IFNAME &&
              iface_ == static_cast<const char*>(RTA_DATA(rta)))
            ours = true;
        }
        if (!ours) continue;
        bool exists = nh->nlmsg_type == RTM_NEWLINK;
        bool up = exists && (ifi->ifi_flags & IFF_UP);
        if (onLink_) onLink_(exists, up);
      }
    }
  }
}

}  // namespace wifimgr
