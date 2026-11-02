#include "ep_linux.h"

#include <errno.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <linux/net_tstamp.h>
#include <net/if.h>
#include <poll.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

ep_status_t ep_linux_can_open(ep_linux_can_t *c, const char *ifname, bool canfd, bool timestamps)
{
    c->fd = socket(PF_CAN, SOCK_RAW | SOCK_CLOEXEC, CAN_RAW);
    c->fd_frames = false;
    if (c->fd < 0)
        return EP_ERR_IO;
    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);
    if (ioctl(c->fd, SIOCGIFINDEX, &ifr) < 0)
        goto fail;
    if (canfd) {
        int on = 1;
        if (setsockopt(c->fd, SOL_CAN_RAW, CAN_RAW_FD_FRAMES, &on, sizeof(on)) == 0)
            c->fd_frames = true;
    }
    if (timestamps) {
        int flags = SOF_TIMESTAMPING_RX_HARDWARE | SOF_TIMESTAMPING_RAW_HARDWARE |
                    SOF_TIMESTAMPING_RX_SOFTWARE | SOF_TIMESTAMPING_SOFTWARE;
        setsockopt(c->fd, SOL_SOCKET, SO_TIMESTAMPING, &flags, sizeof(flags));
    }
    struct sockaddr_can addr;
    memset(&addr, 0, sizeof(addr));
    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;
    if (bind(c->fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
        goto fail;
    return EP_OK;
fail:
    close(c->fd);
    c->fd = -1;
    return EP_ERR_IO;
}

void ep_linux_can_close(ep_linux_can_t *c)
{
    if (c->fd >= 0)
        close(c->fd);
    c->fd = -1;
}

ep_status_t ep_linux_can_send(ep_linux_can_t *c, const ep_can_frame_t *f)
{
    struct canfd_frame k;
    memset(&k, 0, sizeof(k));
    k.can_id = f->id;
    if (f->flags & EP_CAN_FLAG_EXT) k.can_id |= CAN_EFF_FLAG;
    if (f->flags & EP_CAN_FLAG_RTR) k.can_id |= CAN_RTR_FLAG;
    k.len = f->len;
    memcpy(k.data, f->data, f->len);
    size_t sz = CAN_MTU;
    if (f->flags & EP_CAN_FLAG_FD) {
        if (!c->fd_frames)
            return EP_ERR_UNSUPPORTED;
        if (f->flags & EP_CAN_FLAG_BRS) k.flags |= CANFD_BRS;
        if (f->flags & EP_CAN_FLAG_ESI) k.flags |= CANFD_ESI;
        sz = CANFD_MTU;
    }
    ssize_t n = write(c->fd, &k, sz);
    if (n == (ssize_t)sz)
        return EP_OK;
    return (errno == ENOBUFS || errno == EAGAIN) ? EP_ERR_BUSY : EP_ERR_IO;
}

ep_status_t ep_linux_can_recv(ep_linux_can_t *c, ep_can_frame_t *f, int timeout_ms)
{
    struct pollfd p = {.fd = c->fd, .events = POLLIN};
    int r = poll(&p, 1, timeout_ms);
    if (r == 0)
        return EP_ERR_AGAIN;
    if (r < 0)
        return EP_ERR_IO;

    struct canfd_frame k;
    char ctrl[CMSG_SPACE(3 * sizeof(struct timespec))];
    struct iovec iov = {.iov_base = &k, .iov_len = sizeof(k)};
    struct msghdr msg;
    memset(&msg, 0, sizeof(msg));
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = ctrl;
    msg.msg_controllen = sizeof(ctrl);
    ssize_t n = recvmsg(c->fd, &msg, 0);
    if (n != CAN_MTU && n != CANFD_MTU)
        return EP_ERR_IO;

    memset(f, 0, sizeof(*f));
    if (k.can_id & CAN_ERR_FLAG)
        return EP_ERR_FRAME; /* error frame (if enabled via CAN_RAW_ERR_FILTER) */
    f->flags = 0;
    if (k.can_id & CAN_EFF_FLAG) {
        f->flags |= EP_CAN_FLAG_EXT;
        f->id = k.can_id & CAN_EFF_MASK;
    } else {
        f->id = k.can_id & CAN_SFF_MASK;
    }
    if (k.can_id & CAN_RTR_FLAG) f->flags |= EP_CAN_FLAG_RTR;
    if (n == CANFD_MTU) {
        f->flags |= EP_CAN_FLAG_FD;
        if (k.flags & CANFD_BRS) f->flags |= EP_CAN_FLAG_BRS;
        if (k.flags & CANFD_ESI) f->flags |= EP_CAN_FLAG_ESI;
    }
    f->len = k.len > 64 ? 64 : k.len;
    memcpy(f->data, k.data, f->len);

    for (struct cmsghdr *cm = CMSG_FIRSTHDR(&msg); cm; cm = CMSG_NXTHDR(&msg, cm)) {
        if (cm->cmsg_level == SOL_SOCKET && cm->cmsg_type == SO_TIMESTAMPING) {
            struct timespec ts[3];
            memcpy(ts, CMSG_DATA(cm), sizeof(ts));
            struct timespec *best = (ts[2].tv_sec || ts[2].tv_nsec) ? &ts[2] : &ts[0]; /* hw, else sw */
            f->timestamp_ns = (uint64_t)best->tv_sec * 1000000000ull + (uint64_t)best->tv_nsec;
        }
    }
    return EP_OK;
}

ep_status_t ep_linux_can_set_filters(ep_linux_can_t *c, const ep_can_filter_t *f, size_t n)
{
    struct can_filter k[EP_CAN_MAX_SW_FILTERS];
    if (n > EP_CAN_MAX_SW_FILTERS)
        return EP_ERR_OVERFLOW;
    for (size_t i = 0; i < n; i++) {
        k[i].can_id = f[i].id | (f[i].ext ? CAN_EFF_FLAG : 0);
        /* match the EFF bit exactly (std vs ext), leave RTR open: data + remote */
        k[i].can_mask = (f[i].mask & (f[i].ext ? CAN_EFF_MASK : CAN_SFF_MASK)) | CAN_EFF_FLAG;
    }
    if (setsockopt(c->fd, SOL_CAN_RAW, CAN_RAW_FILTER, n ? k : NULL, (socklen_t)(n * sizeof(k[0]))) < 0)
        return EP_ERR_IO;
    return EP_OK;
}

static ep_status_t ops_send(void *ctx, const ep_can_frame_t *f)
{
    return ep_linux_can_send(ctx, f);
}

static ep_status_t ops_filters(void *ctx, const ep_can_filter_t *f, size_t n)
{
    return ep_linux_can_set_filters(ctx, f, n);
}

const ep_can_ops_t ep_linux_can_ops = {
    .set_bittiming = NULL, /* `ip link set can0 type can bitrate 500000` */
    .set_mode = NULL,
    .send = ops_send,
    .get_error_counters = NULL,
    .set_hw_filters = ops_filters,
};
