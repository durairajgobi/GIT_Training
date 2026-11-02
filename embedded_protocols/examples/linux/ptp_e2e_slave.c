/*
 * ptp_e2e_slave - IEEE 1588 ordinary clock (slave only, E2E, L2 transport)
 * disciplining the NIC's PTP hardware clock.
 *
 *   sudo ./ptp_e2e_slave eth0 [domain]
 *
 * Needs a NIC with hardware timestamping (i210, i225, stmmac/EQoS, FEC,
 * CPSW, ...): check with `ethtool -T eth0`. Pair with `phc2sys -s eth0 -w`
 * to steer CLOCK_REALTIME from the PHC.
 */
#include "ep/ep_eth.h"
#include "ep/ep_ptp.h"
#include "ep_linux.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static volatile sig_atomic_t running = 1;
static void on_sig(int s) { (void)s; running = 0; }

typedef struct {
    ep_linux_raw_t raw;
} ctx_t;

static ep_status_t send_ptp(void *arg, const uint8_t *msg, size_t len, bool event, int64_t *tx_ts)
{
    ctx_t *c = arg;
    uint8_t frame[128];
    ep_eth_hdr_t h = {.ethertype = EP_ETHERTYPE_PTP};
    memcpy(h.dst, EP_PTP_MAC_PRIMARY, 6);
    memcpy(h.src, c->raw.mac, 6);
    size_t n = ep_eth_build(frame, sizeof(frame), &h, msg, len);
    return ep_linux_raw_send(&c->raw, frame, n, event ? tx_ts : NULL);
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s <iface> [domain]\n", argv[0]);
        return 2;
    }
    const char *ifname = argv[1];
    ctx_t ctx;
    ep_linux_phc_t phc;

    if (ep_linux_raw_open(&ctx.raw, ifname, EP_ETHERTYPE_PTP, true) != EP_OK) {
        perror("raw socket (needs CAP_NET_RAW)");
        return 1;
    }
    if (!ctx.raw.hw_ts)
        fprintf(stderr, "warning: no hardware timestamping, accuracy will be poor\n");
    ep_linux_raw_join(&ctx.raw, EP_PTP_MAC_PRIMARY);
    if (ep_linux_phc_open_iface(&phc, ifname) != EP_OK) {
        fprintf(stderr, "%s has no PTP hardware clock\n", ifname);
        return 1;
    }

    ep_ptp_oc_config_t cfg;
    ep_ptp_oc_config_default(&cfg);
    ep_ptp_clock_id_from_mac(cfg.self.clock_id, ctx.raw.mac);
    cfg.domain = (uint8_t)(argc > 2 ? atoi(argv[2]) : 0);
    cfg.clock = ep_linux_phc_clock_ops(&phc);
    cfg.send = send_ptp;
    cfg.send_arg = &ctx;
    ep_ptp_oc_t oc;
    ep_ptp_oc_init(&oc, &cfg);
    oc.servo.max_ppb = phc.max_ppb;

    signal(SIGINT, on_sig);
    signal(SIGTERM, on_sig);
    uint32_t last_syncs = 0;
    while (running) {
        uint8_t buf[1600];
        size_t len;
        int64_t rx_ts = 0;
        ep_status_t st = ep_linux_raw_recv(&ctx.raw, buf, sizeof(buf), &len, &rx_ts, 100);
        if (st == EP_OK) {
            ep_eth_hdr_t h;
            if (ep_eth_parse(buf, len, &h) == EP_OK && h.ethertype == EP_ETHERTYPE_PTP)
                ep_ptp_oc_rx(&oc, h.payload, h.payload_len, rx_ts);
        }
        ep_ptp_oc_poll(&oc, cfg.clock.now(cfg.clock.ctx));
        if (oc.syncs != last_syncs && oc.have_delay) {
            last_syncs = oc.syncs;
            static const char *states[] = {"LISTENING", "UNCALIBRATED", "SLAVE"};
            printf("%-12s offset %8lld ns  freq %+10.1f ppb  path %6lld ns\n", states[oc.state],
                   (long long)oc.last_offset, oc.last_ppb, (long long)oc.mean_path_delay);
            fflush(stdout);
        }
    }
    ep_linux_phc_close(&phc);
    ep_linux_raw_close(&ctx.raw);
    return 0;
}
