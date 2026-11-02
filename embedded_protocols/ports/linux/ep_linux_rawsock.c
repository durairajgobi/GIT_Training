#include "ep_linux.h"

#include <arpa/inet.h>
#include <errno.h>
#include <linux/errqueue.h>
#include <linux/if_packet.h>
#include <linux/net_tstamp.h>
#include <linux/sockios.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <poll.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#ifndef SO_TXTIME
#define SO_TXTIME 61
#define SCM_TXTIME SO_TXTIME
#endif

static int64_t ts_to_ns(const struct timespec *t)
{
    return (int64_t)t->tv_sec * 1000000000LL + t->tv_nsec;
}

static bool enable_hw_timestamping(int fd, const char *ifname)
{
    struct hwtstamp_config cfg;
    struct ifreq ifr;
    memset(&cfg, 0, sizeof(cfg));
    memset(&ifr, 0, sizeof(ifr));
    cfg.tx_type = HWTSTAMP_TX_ON;
    cfg.rx_filter = HWTSTAMP_FILTER_PTP_V2_EVENT;
    strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);
    ifr.ifr_data = (char *)&cfg;
    if (ioctl(fd, SIOCSHWTSTAMP, &ifr) < 0) {
        cfg.rx_filter = HWTSTAMP_FILTER_ALL; /* some NICs only do "all" */
        if (ioctl(fd, SIOCSHWTSTAMP, &ifr) < 0)
            return false;
    }
    return true;
}

ep_status_t ep_linux_raw_open(ep_linux_raw_t *r, const char *ifname, uint16_t ethertype, bool hw)
{
    memset(r, 0, sizeof(*r));
    r->fd = socket(AF_PACKET, SOCK_RAW | SOCK_CLOEXEC, htons(ethertype));
    if (r->fd < 0)
        return EP_ERR_IO;

    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);
    if (ioctl(r->fd, SIOCGIFINDEX, &ifr) < 0)
        goto fail;
    r->ifindex = ifr.ifr_ifindex;
    if (ioctl(r->fd, SIOCGIFHWADDR, &ifr) < 0)
        goto fail;
    memcpy(r->mac, ifr.ifr_hwaddr.sa_data, 6);

    struct sockaddr_ll sll;
    memset(&sll, 0, sizeof(sll));
    sll.sll_family = AF_PACKET;
    sll.sll_protocol = htons(ethertype);
    sll.sll_ifindex = r->ifindex;
    if (bind(r->fd, (struct sockaddr *)&sll, sizeof(sll)) < 0)
        goto fail;

    int flags = SOF_TIMESTAMPING_TX_SOFTWARE | SOF_TIMESTAMPING_RX_SOFTWARE | SOF_TIMESTAMPING_SOFTWARE;
    if (hw && enable_hw_timestamping(r->fd, ifname)) {
        flags = SOF_TIMESTAMPING_TX_HARDWARE | SOF_TIMESTAMPING_RX_HARDWARE |
                SOF_TIMESTAMPING_RAW_HARDWARE;
        r->hw_ts = true;
    }
    flags |= SOF_TIMESTAMPING_OPT_TSONLY; /* error queue returns only the stamp */
    if (setsockopt(r->fd, SOL_SOCKET, SO_TIMESTAMPING, &flags, sizeof(flags)) < 0)
        goto fail;
    return EP_OK;
fail:
    close(r->fd);
    r->fd = -1;
    return EP_ERR_IO;
}

void ep_linux_raw_close(ep_linux_raw_t *r)
{
    if (r->fd >= 0)
        close(r->fd);
    r->fd = -1;
}

ep_status_t ep_linux_raw_join(ep_linux_raw_t *r, const uint8_t mac[6])
{
    struct packet_mreq mr;
    memset(&mr, 0, sizeof(mr));
    mr.mr_ifindex = r->ifindex;
    mr.mr_type = PACKET_MR_MULTICAST;
    mr.mr_alen = 6;
    memcpy(mr.mr_address, mac, 6);
    return setsockopt(r->fd, SOL_PACKET, PACKET_ADD_MEMBERSHIP, &mr, sizeof(mr)) < 0 ? EP_ERR_IO : EP_OK;
}

static bool extract_ts(struct msghdr *msg, bool hw, int64_t *out)
{
    for (struct cmsghdr *cm = CMSG_FIRSTHDR(msg); cm; cm = CMSG_NXTHDR(msg, cm)) {
        if (cm->cmsg_level == SOL_SOCKET && cm->cmsg_type == SO_TIMESTAMPING) {
            struct timespec ts[3];
            memcpy(ts, CMSG_DATA(cm), sizeof(ts));
            const struct timespec *t = hw ? &ts[2] : &ts[0];
            if (t->tv_sec == 0 && t->tv_nsec == 0)
                return false;
            *out = ts_to_ns(t);
            return true;
        }
    }
    return false;
}

static ep_status_t fetch_tx_timestamp(ep_linux_raw_t *r, int64_t *tx_ts)
{
    /* The driver loops the stamp back on the error queue, usually within
     * tens of microseconds; give slow NICs up to 20 ms. */
    for (int tries = 0; tries < 20; tries++) {
        struct pollfd p = {.fd = r->fd, .events = POLLPRI};
        if (poll(&p, 1, 1) < 0 && errno != EINTR)
            return EP_ERR_IO;
        uint8_t data[256];
        char ctrl[512];
        struct iovec iov = {.iov_base = data, .iov_len = sizeof(data)};
        struct msghdr msg;
        memset(&msg, 0, sizeof(msg));
        msg.msg_iov = &iov;
        msg.msg_iovlen = 1;
        msg.msg_control = ctrl;
        msg.msg_controllen = sizeof(ctrl);
        if (recvmsg(r->fd, &msg, MSG_ERRQUEUE | MSG_DONTWAIT) < 0) {
            if (errno == EAGAIN || errno == EINTR)
                continue;
            return EP_ERR_IO;
        }
        if (extract_ts(&msg, r->hw_ts, tx_ts))
            return EP_OK;
    }
    return EP_ERR_TIMEOUT;
}

ep_status_t ep_linux_raw_send(ep_linux_raw_t *r, const uint8_t *frame, size_t len, int64_t *tx_ts)
{
    struct sockaddr_ll sll;
    memset(&sll, 0, sizeof(sll));
    sll.sll_family = AF_PACKET;
    sll.sll_ifindex = r->ifindex;
    sll.sll_halen = 6;
    memcpy(sll.sll_addr, frame, 6);
    if (sendto(r->fd, frame, len, 0, (struct sockaddr *)&sll, sizeof(sll)) != (ssize_t)len)
        return errno == ENOBUFS ? EP_ERR_BUSY : EP_ERR_IO;
    return tx_ts ? fetch_tx_timestamp(r, tx_ts) : EP_OK;
}

ep_status_t ep_linux_raw_recv(ep_linux_raw_t *r, uint8_t *buf, size_t cap, size_t *len,
                              int64_t *rx_ts, int timeout_ms)
{
    struct pollfd p = {.fd = r->fd, .events = POLLIN};
    int pr = poll(&p, 1, timeout_ms);
    if (pr == 0)
        return EP_ERR_AGAIN;
    if (pr < 0)
        return errno == EINTR ? EP_ERR_AGAIN : EP_ERR_IO;
    char ctrl[512];
    struct iovec iov = {.iov_base = buf, .iov_len = cap};
    struct msghdr msg;
    memset(&msg, 0, sizeof(msg));
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = ctrl;
    msg.msg_controllen = sizeof(ctrl);
    ssize_t n = recvmsg(r->fd, &msg, 0);
    if (n < 0)
        return EP_ERR_IO;
    *len = (size_t)n;
    if (rx_ts && !extract_ts(&msg, r->hw_ts, rx_ts))
        *rx_ts = 0;
    return (msg.msg_flags & MSG_TRUNC) ? EP_ERR_OVERFLOW : EP_OK;
}

ep_status_t ep_linux_raw_enable_txtime(ep_linux_raw_t *r, clockid_t clk, bool deadline_mode)
{
    struct {
        clockid_t clockid;
        uint32_t flags;
    } cfg = {clk, deadline_mode ? 1u /* SOF_TXTIME_DEADLINE_MODE */ : 0u};
    return setsockopt(r->fd, SOL_SOCKET, SO_TXTIME, &cfg, sizeof(cfg)) < 0 ? EP_ERR_UNSUPPORTED : EP_OK;
}

ep_status_t ep_linux_raw_send_at(ep_linux_raw_t *r, const uint8_t *frame, size_t len,
                                 uint64_t txtime_ns, int priority)
{
    if (setsockopt(r->fd, SOL_SOCKET, SO_PRIORITY, &priority, sizeof(priority)) < 0)
        return EP_ERR_IO;
    struct sockaddr_ll sll;
    memset(&sll, 0, sizeof(sll));
    sll.sll_family = AF_PACKET;
    sll.sll_ifindex = r->ifindex;
    sll.sll_halen = 6;
    memcpy(sll.sll_addr, frame, 6);

    char ctrl[CMSG_SPACE(sizeof(uint64_t))];
    memset(ctrl, 0, sizeof(ctrl));
    struct iovec iov = {.iov_base = (void *)frame, .iov_len = len};
    struct msghdr msg;
    memset(&msg, 0, sizeof(msg));
    msg.msg_name = &sll;
    msg.msg_namelen = sizeof(sll);
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = ctrl;
    msg.msg_controllen = sizeof(ctrl);
    struct cmsghdr *cm = CMSG_FIRSTHDR(&msg);
    cm->cmsg_level = SOL_SOCKET;
    cm->cmsg_type = SCM_TXTIME;
    cm->cmsg_len = CMSG_LEN(sizeof(uint64_t));
    memcpy(CMSG_DATA(cm), &txtime_ns, sizeof(txtime_ns));
    return sendmsg(r->fd, &msg, 0) == (ssize_t)len ? EP_OK : EP_ERR_IO;
}
