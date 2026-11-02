#include "ep/ep_eth.h"
#include "ep/ep_tsn.h"
#include "ep_test.h"

#define US 1000ull
#define GBPS 1000000000ull

/* 500 us cycle: 100 us exclusive window for TC7 (control loop traffic),
 * then 400 us for everything else. */
static const ep_tsn_gcl_entry_t gcl[] = {
    {EP_GCL_SET_GATES, 0x80, 100000},
    {EP_GCL_SET_GATES, 0x7F, 400000},
};

static void test_tas_gates(void)
{
    ep_tsn_tas_t tas;
    const uint64_t base = 1000000;
    CHECK_EQ(ep_tsn_tas_init(&tas, gcl, 2, base, 0, GBPS), EP_OK);
    CHECK_EQ(tas.cycle_time_ns, 500 * US);

    uint64_t end;
    size_t idx;
    CHECK_EQ(ep_tsn_tas_gates(&tas, base - 1, &end, &idx), 0xFF); /* before base */
    CHECK_EQ(end, base);
    CHECK_EQ(ep_tsn_tas_gates(&tas, base, &end, &idx), 0x80);
    CHECK_EQ(idx, 0);
    CHECK_EQ(end, base + 100 * US);
    CHECK_EQ(ep_tsn_tas_gates(&tas, base + 100 * US, &end, &idx), 0x7F);
    CHECK_EQ(end, base + 500 * US);
    CHECK_EQ(ep_tsn_tas_gates(&tas, base + 10 * 500 * US + 50 * US, &end, NULL), 0x80);

    CHECK_EQ(ep_tsn_tas_next_cycle_start(&tas, base + 1), base + 500 * US);
    CHECK_EQ(ep_tsn_tas_next_cycle_start(&tas, 0), base);
}

static void test_tas_guard_band(void)
{
    ep_tsn_tas_t tas;
    const uint64_t base = 0;
    ep_tsn_tas_init(&tas, gcl, 2, base, 0, GBPS);
    uint64_t t = 90 * US; /* 10 us before TC7's gate closes */
    uint64_t close_at;
    CHECK(ep_tsn_tas_gate_open(&tas, t, 7, &close_at));
    CHECK_EQ(close_at, 100 * US);
    CHECK(ep_tsn_tas_can_tx(&tas, t, 7, 64));     /* 672 ns fits    */
    CHECK(!ep_tsn_tas_can_tx(&tas, t, 7, 1514));  /* 12.3 us won't  */
    CHECK_EQ(ep_tsn_tas_next_tx(&tas, t, 7, 1514), 500 * US); /* next cycle */
    CHECK_EQ(ep_tsn_tas_next_tx(&tas, 0, 0, 1514), 100 * US); /* best effort waits */
    CHECK(!ep_tsn_tas_can_tx(&tas, 50 * US, 3, 64));

    /* a class whose gate never closes */
    static const ep_tsn_gcl_entry_t always[] = {{0, 0xFF, 1000}, {0, 0x81, 1000}};
    ep_tsn_tas_t t2;
    ep_tsn_tas_init(&t2, always, 2, 0, 0, GBPS);
    CHECK(ep_tsn_tas_gate_open(&t2, 5, 7, &close_at));
    CHECK(close_at == EP_TSN_NEVER);
    CHECK(ep_tsn_tas_gate_open(&t2, 5, 3, &close_at));
    CHECK_EQ(close_at, 1000);
}

static void test_tas_cycle_extension_truncation(void)
{
    ep_tsn_tas_t tas;
    ep_tsn_tas_init(&tas, gcl, 2, 0, 600 * US, GBPS); /* longer: last entry extended */
    uint64_t end;
    CHECK_EQ(ep_tsn_tas_gates(&tas, 550 * US, &end, NULL), 0x7F);
    CHECK_EQ(end, 600 * US);
    ep_tsn_tas_init(&tas, gcl, 2, 0, 300 * US, GBPS); /* shorter: truncated */
    CHECK_EQ(ep_tsn_tas_gates(&tas, 250 * US, &end, NULL), 0x7F);
    CHECK_EQ(end, 300 * US);
    CHECK_EQ(ep_tsn_tas_gates(&tas, 310 * US, &end, NULL), 0x80);
}

static void test_taprio_cmd(void)
{
    ep_tsn_tas_t tas;
    ep_tsn_tas_init(&tas, gcl, 2, 1000000000ull, 0, GBPS);
    const uint8_t map[16] = {1, 0, 2, 3, 4, 5, 6, 7, 0, 0, 0, 0, 0, 0, 0, 0};
    char buf[512];
    int n = ep_tsn_taprio_cmd(&tas, "eth0", 8, map, false, buf, sizeof(buf));
    CHECK(n > 0 && (size_t)n < sizeof(buf));
    CHECK(strstr(buf, "dev eth0") != NULL);
    CHECK(strstr(buf, "map 1 0 2 3 4 5 6 7 0") != NULL);
    CHECK(strstr(buf, "queues 1@0 1@1") != NULL);
    CHECK(strstr(buf, "sched-entry S 80 100000 sched-entry S 7f 400000") != NULL);
    CHECK(strstr(buf, "clockid CLOCK_TAI") != NULL);
    char small[16];
    int n2 = ep_tsn_taprio_cmd(&tas, "eth0", 8, map, true, small, sizeof(small));
    CHECK(n2 > n - 30); /* reports the full length even when truncated */
    CHECK_EQ(small[15], '\0');
}

static void test_cbs_long_run_rate(void)
{
    /* SR class A: 25 Mbit/s reservation on 100 Mbit/s, always backlogged */
    ep_tsn_cbs_t c;
    const uint64_t port = 100000000ull;
    CHECK_EQ(ep_tsn_cbs_init(&c, 25000000ull, port, 1522, 1522), EP_OK);
    CHECK_EQ(c.send_slope_bps, -75000000);
    CHECK_EQ(c.hi_credit_bits, 3044);
    CHECK_EQ(c.lo_credit_bits, -9132);

    uint64_t now = 0, bits = 0;
    const size_t wire = 1000;
    while (now < 1000000000ull) {
        ep_tsn_cbs_update(&c, now, true);
        if (ep_tsn_cbs_eligible(&c)) {
            ep_tsn_cbs_on_tx(&c, now, wire);
            now = c.last_ns; /* link busy for the frame */
            bits += wire * 8;
        } else {
            now += ep_tsn_cbs_time_to_eligible(&c);
        }
        CHECK(ep_tsn_cbs_credit_bits(&c) >= c.lo_credit_bits);
    }
    double rate = (double)bits; /* bits in 1 s */
    CHECK(rate > 24.5e6 && rate < 25.5e6);
}

static void test_cbs_idle_resets_credit(void)
{
    ep_tsn_cbs_t c;
    ep_tsn_cbs_init(&c, 25000000ull, 100000000ull, 1522, 1522);
    ep_tsn_cbs_on_tx(&c, 0, 1000); /* credit negative after a frame */
    CHECK(!ep_tsn_cbs_eligible(&c));
    ep_tsn_cbs_update(&c, 10000000, false); /* idle for 10 ms */
    CHECK_EQ(ep_tsn_cbs_credit_bits(&c), 0); /* recovers to 0, not above */
}

static void test_frer(void)
{
    ep_tsn_frer_t r;
    ep_tsn_frer_init(&r, 16, 0);
    /* two redundant paths deliver interleaved copies; path B lags */
    const uint16_t a[] = {100, 101, 102, 103, 104, 105};
    int passed = 0;
    for (int i = 0; i < 6; i++) {
        passed += ep_tsn_frer_accept(&r, a[i], 0);
        if (i >= 1)
            passed += ep_tsn_frer_accept(&r, a[i - 1], 0); /* late duplicate */
    }
    passed += ep_tsn_frer_accept(&r, 105, 0);
    CHECK_EQ(passed, 6);
    CHECK_EQ(r.discarded, 6);

    /* path A loses 107, path B delivers it late: passes as out-of-order */
    CHECK(ep_tsn_frer_accept(&r, 106, 0));
    CHECK(ep_tsn_frer_accept(&r, 108, 0));
    CHECK(ep_tsn_frer_accept(&r, 107, 0));
    CHECK(!ep_tsn_frer_accept(&r, 107, 0));
    CHECK_EQ(r.out_of_order, 1);

    CHECK(!ep_tsn_frer_accept(&r, 300, 0)); /* rogue: outside the window */
    CHECK_EQ(r.rogue, 1);

    /* sequence number wrap-around */
    ep_tsn_frer_init(&r, 8, 0);
    CHECK(ep_tsn_frer_accept(&r, 65534, 0));
    CHECK(ep_tsn_frer_accept(&r, 65535, 0));
    CHECK(ep_tsn_frer_accept(&r, 0, 0));
    CHECK(!ep_tsn_frer_accept(&r, 65535, 0));
    CHECK(ep_tsn_frer_accept(&r, 1, 0));

    /* recovery timeout resets the window */
    ep_tsn_frer_init(&r, 8, 1000000);
    CHECK(ep_tsn_frer_accept(&r, 10, 0));
    CHECK(!ep_tsn_frer_accept(&r, 5000, 10));
    CHECK(ep_tsn_frer_accept(&r, 5000, 2000000));
    CHECK_EQ(r.resets, 1);
}

static size_t make_frame(ep_tsn_frame_t *f, uint8_t pcp, size_t payload)
{
    static const uint8_t dst[6] = {0x01, 0x80, 0xC2, 0x00, 0x00, 0x0E};
    static const uint8_t src[6] = {0x02, 0, 0, 0, 0, 1};
    ep_eth_hdr_t h = {.ethertype = 0x88B5, .vlan = true, .pcp = pcp, .vid = 10};
    memcpy(h.dst, dst, 6);
    memcpy(h.src, src, 6);
    uint8_t pl[1500] = {0};
    f->len = (uint16_t)ep_eth_build(f->data, sizeof(f->data), &h, pl, payload);
    return f->len;
}

static void test_egress_scheduler(void)
{
    static ep_tsn_egress_t eg; /* large: keep off the stack */
    ep_tsn_tas_t tas;
    ep_tsn_tas_init(&tas, gcl, 2, 0, 0, GBPS);
    ep_tsn_egress_init(&eg, GBPS, &tas);

    ep_tsn_frame_t *be = ep_tsn_egress_alloc(&eg);
    make_frame(be, 0, 1000);
    ep_tsn_frame_t *ctl = ep_tsn_egress_alloc(&eg);
    make_frame(ctl, 7, 46);
    CHECK_EQ(ep_tsn_egress_classify(&eg, be->data, be->len), 1);  /* PCP0 -> TC1 */
    CHECK_EQ(ep_tsn_egress_classify(&eg, ctl->data, ctl->len), 7);

    uint64_t now = 450 * US; /* best-effort window */
    ep_tsn_egress_enqueue(&eg, be, ep_tsn_egress_classify(&eg, be->data, be->len), now);
    ep_tsn_egress_enqueue(&eg, ctl, 7, now);

    /* TC7 gate closed now; BE frame fits before 500 us (8.2 us) */
    ep_tsn_frame_t *f = ep_tsn_egress_dequeue(&eg, now);
    CHECK(f == be);
    ep_tsn_egress_free(&eg, f);
    CHECK(ep_tsn_egress_dequeue(&eg, now) == NULL);
    CHECK_EQ(ep_tsn_egress_next_event(&eg, now), 500 * US); /* TC7 window opens */
    f = ep_tsn_egress_dequeue(&eg, 500 * US);
    CHECK(f == ctl);
    ep_tsn_egress_free(&eg, f);

    /* BE frame that would overrun the gate close is held (guard band) */
    be = ep_tsn_egress_alloc(&eg);
    make_frame(be, 0, 1500);
    ep_tsn_egress_enqueue(&eg, be, 1, 495 * US);
    CHECK(ep_tsn_egress_dequeue(&eg, 495 * US) == NULL);
    CHECK_EQ(ep_tsn_egress_next_event(&eg, 495 * US), 600 * US);
    f = ep_tsn_egress_dequeue(&eg, 600 * US);
    CHECK(f == be);
    ep_tsn_egress_free(&eg, f);

    /* LaunchTime: frame not released before its launch time */
    ep_tsn_frame_t *lt = ep_tsn_egress_alloc(&eg);
    make_frame(lt, 7, 46);
    lt->launch_ns = 1020 * US;
    ep_tsn_egress_enqueue(&eg, lt, 7, 1000 * US);
    CHECK(ep_tsn_egress_dequeue(&eg, 1010 * US) == NULL);
    CHECK_EQ(ep_tsn_egress_next_event(&eg, 1010 * US), 1020 * US);
    CHECK(ep_tsn_egress_dequeue(&eg, 1020 * US) == lt);
    ep_tsn_egress_free(&eg, lt);
}

static void test_egress_cbs_and_pool(void)
{
    static ep_tsn_egress_t eg;
    ep_tsn_cbs_t cbs;
    ep_tsn_egress_init(&eg, 100000000ull, NULL);
    ep_tsn_cbs_init(&cbs, 25000000ull, 100000000ull, 1522, 1522);
    ep_tsn_egress_set_cbs(&eg, 3, &cbs);

    for (int i = 0; i < 3; i++) {
        ep_tsn_frame_t *f = ep_tsn_egress_alloc(&eg);
        make_frame(f, 3, 200);
        CHECK_EQ(ep_tsn_egress_enqueue(&eg, f, 3, 0), EP_OK);
    }
    ep_tsn_frame_t *f = ep_tsn_egress_dequeue(&eg, 0);
    CHECK(f != NULL);
    ep_tsn_egress_free(&eg, f);
    CHECK(ep_tsn_egress_dequeue(&eg, 30 * US) == NULL); /* credit negative */
    uint64_t t = ep_tsn_egress_next_event(&eg, 30 * US);
    CHECK(t > 30 * US);
    f = ep_tsn_egress_dequeue(&eg, t);
    CHECK(f != NULL);
    ep_tsn_egress_free(&eg, f);

    /* per-class queue limit protects the shared pool */
    int ok = 0;
    for (int i = 0; i < EP_TSN_POOL_SIZE; i++) {
        ep_tsn_frame_t *g = ep_tsn_egress_alloc(&eg);
        if (!g)
            break;
        make_frame(g, 0, 46);
        if (ep_tsn_egress_enqueue(&eg, g, 1, 0) == EP_OK)
            ok++;
    }
    CHECK_EQ(ok, EP_TSN_POOL_SIZE / 2);
    CHECK(eg.drops[1] > 0);
}

int main(void)
{
    RUN(test_tas_gates);
    RUN(test_tas_guard_band);
    RUN(test_tas_cycle_extension_truncation);
    RUN(test_taprio_cmd);
    RUN(test_cbs_long_run_rate);
    RUN(test_cbs_idle_resets_credit);
    RUN(test_frer);
    RUN(test_egress_scheduler);
    RUN(test_egress_cbs_and_pool);
    TEST_MAIN_END();
}
