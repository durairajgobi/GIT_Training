/*
 * tsn_taprio_plan - turn a TSN schedule into Linux qdisc commands and a
 * worst-case latency report. Runs anywhere (no privileges needed).
 *
 *   ./tsn_taprio_plan [iface] [link_mbps]
 *
 * Traffic plan (1 ms cycle):
 *   TC7  motion control   PCP 6/7  250 us exclusive window (scheduled)
 *   TC5  audio (SR A)     PCP 3    CBS 20 Mbit/s inside the shared window
 *   TC0-4 best effort              750 us shared window
 */
#include "ep/ep_eth.h"
#include "ep/ep_tsn.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    const char *ifname = argc > 1 ? argv[1] : "eth0";
    uint64_t link = (argc > 2 ? strtoull(argv[2], NULL, 10) : 1000) * 1000000ull;

    static const ep_tsn_gcl_entry_t gcl[] = {
        {EP_GCL_SET_GATES, 0x80, 250000},
        {EP_GCL_SET_GATES, 0x7F, 750000},
    };
    ep_tsn_tas_t tas;
    /* start on a whole second of TAI so every node agrees on cycle phase */
    const uint64_t base = 1000000000ull;
    if (ep_tsn_tas_init(&tas, gcl, 2, base, 0, link) != EP_OK) {
        fprintf(stderr, "bad schedule\n");
        return 1;
    }
    /* priority -> traffic class: 0,1,2 -> TC0..2 ; 3 -> TC5 ; 6,7 -> TC7 */
    const uint8_t map[16] = {0, 1, 2, 5, 3, 4, 7, 7, 0, 0, 0, 0, 0, 0, 0, 0};

    char cmd[1024];
    ep_tsn_taprio_cmd(&tas, ifname, 8, map, false, cmd, sizeof(cmd));
    printf("# 802.1Qbv time-aware shaper (software taprio, CLOCK_TAI)\n%s\n\n", cmd);
    ep_tsn_taprio_cmd(&tas, ifname, 8, map, true, cmd, sizeof(cmd));
    printf("# same schedule, full hardware offload (i225/i226, stmmac, ENETC)\n%s\n\n", cmd);

    ep_tsn_cbs_t cbs;
    ep_tsn_cbs_init(&cbs, 20000000ull, link, 1522, 1522);
    printf("# 802.1Qav credit-based shaper for TC5 (queue 6)\n");
    printf("tc qdisc replace dev %s parent 100:6 cbs idleslope %lld sendslope %lld "
           "hicredit %lld locredit %lld offload 1\n\n",
           ifname, (long long)(cbs.idle_slope_bps / 1000), (long long)(cbs.send_slope_bps / 1000),
           (long long)cbs.hi_credit_bits, (long long)cbs.lo_credit_bits);

    printf("# LaunchTime (ETF) under the TC7 queue for SO_TXTIME senders\n");
    printf("tc qdisc replace dev %s parent 100:8 etf clockid CLOCK_TAI delta 200000 offload skip_sock_check\n\n",
           ifname);

    printf("# Worst case for a 1514-byte TC7 frame released at the worst moment:\n");
    uint64_t worst = 0, worst_at = 0;
    for (uint64_t t = base; t < base + tas.cycle_time_ns; t += 1000) {
        uint64_t start = ep_tsn_tas_next_tx(&tas, t, 7, 1514);
        if (start - t > worst) {
            worst = start - t;
            worst_at = t - base;
        }
    }
    printf("#   max gate wait %.1f us (released at +%.1f us into the cycle)\n", (double)worst / 1000.0,
           (double)worst_at / 1000.0);
    printf("#   wire time     %.1f us at %llu Mbit/s\n", (double)ep_eth_wire_time_ns(1514, link) / 1000.0,
           (unsigned long long)(link / 1000000ull));
    printf("#   guard band: best-effort frames that cannot finish before +750 us are held\n");
    return 0;
}
