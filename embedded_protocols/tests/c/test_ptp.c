#include "ep/ep_ptp.h"
#include "ep_test.h"

#include <math.h>

static void test_ts_and_header_codec(void)
{
    uint8_t ts[10];
    int64_t t = 1700000000LL * EP_NS_PER_SEC + 123456789;
    ep_ptp_ts_pack(ts, t);
    CHECK_EQ(ep_ptp_ts_unpack(ts), t);

    ep_ptp_hdr_t h = {0};
    h.sdo_id_major = 1; /* gPTP */
    h.minor_version = 1;
    h.domain = 24;
    h.flags = EP_PTP_FLAG_TWO_STEP | EP_PTP_FLAG_PTP_TIMESCALE;
    h.correction = ep_ptp_ns_to_corr(-1500);
    memcpy(h.source.clock_id, "\x00\x1b\x19\xff\xfe\x00\x00\x01", 8);
    h.source.port = 1;
    h.sequence_id = 0xBEEF;
    h.log_interval = -3;
    uint8_t buf[64];
    size_t n = ep_ptp_build_ts_msg(buf, sizeof(buf), &h, EP_PTP_SYNC, t);
    CHECK_EQ(n, 44);
    CHECK_EQ(buf[0], 0x10);
    CHECK_EQ(buf[1], 0x12);
    CHECK_EQ(buf[32], 0); /* controlField Sync */

    ep_ptp_hdr_t p;
    CHECK_EQ(ep_ptp_hdr_unpack(buf, n, &p), EP_OK);
    CHECK_EQ(p.msg_type, EP_PTP_SYNC);
    CHECK_EQ(p.sdo_id_major, 1);
    CHECK_EQ(p.domain, 24);
    CHECK_EQ(p.sequence_id, 0xBEEF);
    CHECK_EQ(p.log_interval, -3);
    CHECK_EQ(ep_ptp_corr_to_ns(p.correction), -1500);
    CHECK(ep_ptp_port_id_equal(&p.source, &h.source));
    CHECK_EQ(ep_ptp_body_ts(buf), t);
    CHECK_EQ(ep_ptp_hdr_unpack(buf, 40, &p), EP_ERR_FRAME); /* truncated */
    buf[1] = 0x11; /* PTPv1 */
    CHECK_EQ(ep_ptp_hdr_unpack(buf, n, &p), EP_ERR_UNSUPPORTED);
}

static void test_announce_codec(void)
{
    ep_ptp_hdr_t h = {0};
    ep_ptp_announce_t a = {.utc_offset = 37, .gm_priority1 = 128,
                           .gm_quality = {6, 0x21, 0x4E5D}, .gm_priority2 = 128,
                           .steps_removed = 1, .time_source = 0x20};
    memcpy(a.gm_identity, "GMCLOCK!", 8);
    uint8_t buf[64], pad[64];
    CHECK_EQ(ep_ptp_build_announce(buf, sizeof(buf), &h, &a), 64);
    CHECK_EQ(ep_ptp_build_announce(pad, 63, &h, &a), 0);
    ep_ptp_announce_t b;
    CHECK_EQ(ep_ptp_parse_announce(buf, 64, &b), EP_OK);
    CHECK_EQ(b.utc_offset, 37);
    CHECK_EQ(b.gm_quality.clock_class, 6);
    CHECK_EQ(b.gm_quality.variance, 0x4E5D);
    CHECK_MEM(b.gm_identity, "GMCLOCK!", 8);
    CHECK_EQ(b.steps_removed, 1);
}

static void test_delay_math(void)
{
    /* slave 1000 ns ahead, 500 ns path, 200 ns residence in a TC on Sync */
    int64_t t1 = 10000, t2 = t1 + 500 + 200 + 1000;
    int64_t t3 = 20000, t4 = t3 - 1000 + 500;
    int64_t off, mpd;
    ep_ptp_e2e(t1, t2, t3, t4, 200, 0, &off, &mpd);
    CHECK_EQ(mpd, 500);
    CHECK_EQ(off, 1000);

    /* P2P: 800 ns link, peer turnaround 10 us, equal rates */
    CHECK_EQ(ep_ptp_p2p_link_delay(0, 800, 10800, 11600, 0, 1.0), 800);
    /* neighbour 100 ppm fast: its intervals are longer */
    double r = ep_ptp_rate_ratio(0, 0, 1000100000, 1000000000);
    CHECK(fabs(r - 1.0001) < 1e-9);

    uint8_t mac[6] = {0x00, 0x1B, 0x19, 0xAA, 0xBB, 0xCC}, id[8];
    ep_ptp_clock_id_from_mac(id, mac);
    CHECK_MEM(id, "\x00\x1b\x19\xff\xfe\xaa\xbb\xcc", 8);
}

static void test_bmca(void)
{
    ep_ptp_dataset_t a = {.priority1 = 128, .quality = {248, 0xFE, 0xFFFF}, .priority2 = 128};
    ep_ptp_dataset_t b = a;
    memcpy(a.gm_identity, "AAAAAAAA", 8);
    memcpy(b.gm_identity, "BBBBBBBB", 8);
    CHECK(ep_ptp_bmca_compare(&a, &b) < 0); /* tie broken by identity */
    b.quality.clock_class = 6;              /* GPS-locked grandmaster */
    CHECK(ep_ptp_bmca_compare(&a, &b) > 0);
    a.priority1 = 127;                      /* priority1 beats quality */
    CHECK(ep_ptp_bmca_compare(&a, &b) < 0);

    ep_ptp_dataset_t c = b, d = b; /* same GM, two paths */
    c.steps_removed = 2;
    d.steps_removed = 1;
    CHECK(ep_ptp_bmca_compare(&c, &d) > 0);
    CHECK_EQ(ep_ptp_bmca_compare(&d, &d), 0);
}

static void test_servo_basic(void)
{
    ep_ptp_servo_t s;
    ep_servo_state_t st;
    ep_ptp_servo_init(&s, 0.7, 0.3, 500000);
    /* local clock gains 10 us/s (10 ppm fast), 50 ms ahead */
    ep_ptp_servo_sample(&s, 50000000, 1000000000, &st);
    CHECK_EQ(st, EP_SERVO_UNLOCKED);
    double ppb = ep_ptp_servo_sample(&s, 50010000, 2000000000, &st);
    CHECK_EQ(st, EP_SERVO_JUMP);       /* 50 ms > 20 us first-step threshold */
    CHECK(fabs(ppb + 10000.0) < 1e-6); /* slow down by 10 ppm */
    ppb = ep_ptp_servo_sample(&s, 100, 3000000000ULL, &st);
    CHECK_EQ(st, EP_SERVO_LOCKED);
    CHECK(ppb < -10000.0); /* still ahead: correct a bit more */
}

/* ------------------- closed-loop grandmaster <-> slave -------------- */

typedef struct {
    int64_t true_ns;     /* master / reference time */
    double slave_ns;     /* slave clock phase */
    double err_ppb;      /* oscillator error */
    double adj_ppb;      /* servo correction */
    int64_t path_ns;
    /* pending Delay_Req seen by the "master" */
    bool dreq_seen;
    uint16_t dreq_seq;
    int64_t dreq_t4;
    ep_ptp_port_id_t dreq_src;
} world_t;

static world_t W;

static void advance(int64_t dt)
{
    W.true_ns += dt;
    W.slave_ns += (double)dt * (1.0 + (W.err_ppb + W.adj_ppb) * 1e-9);
}
static int64_t slave_now(void *ctx) { (void)ctx; return (int64_t)llround(W.slave_ns); }
static ep_status_t clk_adj(void *ctx, double ppb) { (void)ctx; W.adj_ppb = ppb; return EP_OK; }
static ep_status_t clk_step(void *ctx, int64_t d) { (void)ctx; W.slave_ns += (double)d; return EP_OK; }

static ep_status_t slave_send(void *arg, const uint8_t *msg, size_t len, bool event, int64_t *tx_ts)
{
    (void)arg;
    ep_ptp_hdr_t h;
    CHECK_EQ(ep_ptp_hdr_unpack(msg, len, &h), EP_OK);
    CHECK(event);
    CHECK_EQ(h.msg_type, EP_PTP_DELAY_REQ);
    *tx_ts = slave_now(NULL);           /* hardware egress timestamp */
    W.dreq_seen = true;
    W.dreq_seq = h.sequence_id;
    W.dreq_src = h.source;
    W.dreq_t4 = W.true_ns + W.path_ns;  /* master ingress timestamp */
    return EP_OK;
}

static ep_ptp_hdr_t gm_hdr(uint16_t seq)
{
    ep_ptp_hdr_t h = {0};
    h.version = 2;
    h.minor_version = 1;
    memcpy(h.source.clock_id, "\xAA\xBB\xCC\xFF\xFE\x00\x00\x01", 8);
    h.source.port = 1;
    h.sequence_id = seq;
    return h;
}

static void gm_announce(ep_ptp_oc_t *oc, uint16_t seq)
{
    uint8_t m[64];
    ep_ptp_hdr_t h = gm_hdr(seq);
    ep_ptp_announce_t a = {.gm_priority1 = 128, .gm_quality = {6, 0x21, 0x4E5D}, .gm_priority2 = 128};
    memcpy(a.gm_identity, h.source.clock_id, 8);
    ep_ptp_build_announce(m, sizeof(m), &h, &a);
    ep_ptp_oc_rx(oc, m, 64, 0);
}

static void gm_sync_cycle(ep_ptp_oc_t *oc, uint16_t seq)
{
    uint8_t m[64];
    /* two-step Sync */
    ep_ptp_hdr_t h = gm_hdr(seq);
    h.flags = EP_PTP_FLAG_TWO_STEP;
    int64_t t1 = W.true_ns;
    size_t n = ep_ptp_build_ts_msg(m, sizeof(m), &h, EP_PTP_SYNC, 0);
    advance(W.path_ns);
    W.dreq_seen = false;
    ep_ptp_oc_rx(oc, m, n, slave_now(NULL));
    advance(20000);
    h = gm_hdr(seq);
    n = ep_ptp_build_ts_msg(m, sizeof(m), &h, EP_PTP_FOLLOW_UP, t1);
    ep_ptp_oc_rx(oc, m, n, 0); /* triggers servo + Delay_Req */
    advance(50000);
    if (W.dreq_seen) {
        h = gm_hdr(W.dreq_seq);
        n = ep_ptp_build_resp(m, sizeof(m), &h, EP_PTP_DELAY_RESP, W.dreq_t4, &W.dreq_src);
        ep_ptp_oc_rx(oc, m, n, 0);
    }
}

static void test_oc_converges(void)
{
    memset(&W, 0, sizeof(W));
    W.true_ns = 1000LL * EP_NS_PER_SEC;
    W.slave_ns = (double)W.true_ns + 1.5e6; /* 1.5 ms off */
    W.err_ppb = 50000.0;                    /* 50 ppm fast crystal */
    W.path_ns = 10000;

    ep_ptp_oc_config_t cfg;
    ep_ptp_oc_config_default(&cfg);
    memcpy(cfg.self.clock_id, "\x02\x00\x00\xFF\xFE\x00\x00\x02", 8);
    cfg.clock.adj_freq = clk_adj;
    cfg.clock.step = clk_step;
    cfg.clock.now = slave_now;
    cfg.send = slave_send;
    cfg.log_announce_interval = 0;
    ep_ptp_oc_t oc;
    CHECK_EQ(ep_ptp_oc_init(&oc, &cfg), EP_OK);

    gm_sync_cycle(&oc, 1); /* no master yet: ignored */
    CHECK_EQ(oc.syncs, 0);
    gm_announce(&oc, 1);
    CHECK_EQ(oc.master_idx, -1); /* one Announce does not qualify */
    gm_announce(&oc, 2);
    CHECK_EQ(oc.master_idx, 0);
    CHECK_EQ(oc.state, EP_PTP_PORT_UNCALIBRATED);

    int64_t max_abs_late = 0;
    for (uint16_t seq = 2; seq < 400; seq++) {
        if (seq % 8 == 0)
            gm_announce(&oc, (uint16_t)(seq / 8 + 2));
        gm_sync_cycle(&oc, seq);
        advance(125000000 - W.path_ns - 70000); /* 8 Sync/s */
        ep_ptp_oc_poll(&oc, slave_now(NULL));
        if (seq > 300) {
            int64_t real = (int64_t)llround(W.slave_ns) - W.true_ns;
            if (llabs(real) > max_abs_late)
                max_abs_late = llabs(real);
        }
    }
    CHECK_EQ(oc.state, EP_PTP_PORT_SLAVE);
    CHECK_EQ(oc.steps, 1);
    CHECK(llabs(oc.mean_path_delay - 10000) <= 20);
    CHECK(llabs(oc.last_offset) <= 20);
    CHECK(max_abs_late <= 20);                        /* true error vs. reference */
    CHECK(fabs(W.adj_ppb + W.err_ppb) < 50.0);        /* learned the 50 ppm */
    printf("  locked: offset %lld ns, path %lld ns, freq %.1f ppb, worst %lld ns\n",
           (long long)oc.last_offset, (long long)oc.mean_path_delay, W.adj_ppb,
           (long long)max_abs_late);

    /* grandmaster disappears: announce receipt timeout (3 x 1 s) */
    advance(4 * EP_NS_PER_SEC);
    ep_ptp_oc_poll(&oc, slave_now(NULL));
    CHECK_EQ(oc.master_idx, -1);
    CHECK_EQ(oc.state, EP_PTP_PORT_LISTENING);
}

int main(void)
{
    RUN(test_ts_and_header_codec);
    RUN(test_announce_codec);
    RUN(test_delay_math);
    RUN(test_bmca);
    RUN(test_servo_basic);
    RUN(test_oc_converges);
    TEST_MAIN_END();
}
