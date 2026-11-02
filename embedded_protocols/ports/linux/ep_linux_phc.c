#include "ep_linux.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/ethtool.h>
#include <linux/ptp_clock.h>
#include <linux/sockios.h>
#include <net/if.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/timex.h>
#include <unistd.h>

#define FD_TO_CLOCKID(fd) ((clockid_t)((((unsigned int)~(fd)) << 3) | 3))

#ifndef ADJ_SETOFFSET
#define ADJ_SETOFFSET 0x0100
#endif
#ifndef ADJ_NANO
#define ADJ_NANO 0x2000
#endif

ep_status_t ep_linux_phc_open(ep_linux_phc_t *p, const char *path)
{
    p->fd = open(path, O_RDWR | O_CLOEXEC);
    if (p->fd < 0)
        return EP_ERR_IO;
    p->clk = FD_TO_CLOCKID(p->fd);
    struct ptp_clock_caps caps;
    memset(&caps, 0, sizeof(caps));
    p->max_ppb = 500000.0;
    if (ioctl(p->fd, PTP_CLOCK_GETCAPS, &caps) == 0 && caps.max_adj > 0)
        p->max_ppb = caps.max_adj;
    return EP_OK;
}

ep_status_t ep_linux_phc_open_iface(ep_linux_phc_t *p, const char *ifname)
{
    int s = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (s < 0)
        return EP_ERR_IO;
    struct ethtool_ts_info info;
    struct ifreq ifr;
    memset(&info, 0, sizeof(info));
    memset(&ifr, 0, sizeof(ifr));
    info.cmd = ETHTOOL_GET_TS_INFO;
    strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);
    ifr.ifr_data = (char *)&info;
    int r = ioctl(s, SIOCETHTOOL, &ifr);
    close(s);
    if (r < 0 || info.phc_index < 0)
        return EP_ERR_UNSUPPORTED; /* NIC without a PTP hardware clock */
    char path[32];
    snprintf(path, sizeof(path), "/dev/ptp%d", info.phc_index);
    return ep_linux_phc_open(p, path);
}

void ep_linux_phc_close(ep_linux_phc_t *p)
{
    if (p->fd >= 0)
        close(p->fd);
    p->fd = -1;
}

static int64_t phc_now(void *ctx)
{
    ep_linux_phc_t *p = ctx;
    struct timespec ts;
    if (clock_gettime(p->clk, &ts) < 0)
        return 0;
    return (int64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

static ep_status_t phc_adj_freq(void *ctx, double ppb)
{
    ep_linux_phc_t *p = ctx;
    if (ppb > p->max_ppb) ppb = p->max_ppb;
    if (ppb < -p->max_ppb) ppb = -p->max_ppb;
    struct timex tx;
    memset(&tx, 0, sizeof(tx));
    tx.modes = ADJ_FREQUENCY;
    tx.freq = (long)(ppb * 65.536); /* scaled ppm: ppm * 2^16 */
    return clock_adjtime(p->clk, &tx) < 0 ? EP_ERR_IO : EP_OK;
}

static ep_status_t phc_step(void *ctx, int64_t delta)
{
    ep_linux_phc_t *p = ctx;
    struct timex tx;
    memset(&tx, 0, sizeof(tx));
    tx.modes = ADJ_SETOFFSET | ADJ_NANO;
    int64_t sec = delta / 1000000000LL, nsec = delta % 1000000000LL;
    if (nsec < 0) { /* tv_usec (ns with ADJ_NANO) must be non-negative */
        sec -= 1;
        nsec += 1000000000LL;
    }
    tx.time.tv_sec = (time_t)sec;
    tx.time.tv_usec = (long)nsec;
    return clock_adjtime(p->clk, &tx) < 0 ? EP_ERR_IO : EP_OK;
}

ep_ptp_clock_ops_t ep_linux_phc_clock_ops(ep_linux_phc_t *p)
{
    ep_ptp_clock_ops_t ops = {phc_adj_freq, phc_step, phc_now, p};
    return ops;
}
