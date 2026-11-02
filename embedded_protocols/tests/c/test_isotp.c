#include "ep/ep_isotp.h"
#include "ep_test.h"

/* Two ISO-TP endpoints (tester 0x7E0 <-> ECU 0x7E8) on a simulated bus. */
typedef struct {
    ep_can_frame_t q[256];
    uint64_t at[256];
    size_t head, tail;
    uint64_t now;
    int drop_cf_index; /* drop the n-th CF (1-based) to test seq errors */
    int cf_seen;
    bool drop_fc;
} bus_t;

typedef struct {
    bus_t *bus;
    ep_isotp_t tp;
    uint8_t rx[4096];
    size_t rx_len;
    int rx_count;
    ep_status_t tx_result;
    int tx_done;
} node_t;

static ep_status_t bus_send(void *arg, const ep_can_frame_t *f)
{
    node_t *n = arg;
    bus_t *b = n->bus;
    if ((f->data[0] >> 4) == 2 && ++b->cf_seen == b->drop_cf_index)
        return EP_OK; /* lost on the wire */
    if ((f->data[0] >> 4) == 3 && b->drop_fc)
        return EP_OK;
    b->at[b->head % 256] = b->now;
    b->q[b->head++ % 256] = *f;
    return EP_OK;
}
static void on_rx(void *arg, const uint8_t *d, size_t len)
{
    node_t *n = arg;
    memcpy(n->rx, d, len);
    n->rx_len = len;
    n->rx_count++;
}
static void on_tx(void *arg, ep_status_t r)
{
    node_t *n = arg;
    n->tx_result = r;
    n->tx_done++;
}

static void node_init(node_t *n, bus_t *b, uint32_t tx, uint32_t rx, uint8_t bs, uint8_t stmin, size_t cap)
{
    memset(n, 0, sizeof(*n));
    n->bus = b;
    ep_isotp_config_t c;
    ep_isotp_config_default(&c);
    c.tx_id = tx;
    c.rx_id = rx;
    c.block_size = bs;
    c.st_min = stmin;
    c.send = bus_send;
    c.on_rx = on_rx;
    c.on_tx_done = on_tx;
    c.arg = n;
    c.rx_buf = n->rx;
    c.rx_cap = cap;
    CHECK_EQ(ep_isotp_init(&n->tp, &c), EP_OK);
}

/* deliver frames and advance time in 100 us steps */
static void pump(bus_t *b, node_t *a, node_t *e, uint64_t until)
{
    while (b->now < until) {
        while (b->tail != b->head) {
            ep_can_frame_t f = b->q[b->tail++ % 256];
            ep_isotp_on_frame(&a->tp, &f, b->now);
            ep_isotp_on_frame(&e->tp, &f, b->now);
        }
        ep_isotp_poll(&a->tp, b->now);
        ep_isotp_poll(&e->tp, b->now);
        b->now += 100000;
    }
}

static void test_single_frame(void)
{
    bus_t b = {0};
    node_t tester, ecu;
    node_init(&tester, &b, 0x7E0, 0x7E8, 8, 0, 4095);
    node_init(&ecu, &b, 0x7E8, 0x7E0, 8, 0, 4095);
    const uint8_t req[] = {0x22, 0xF1, 0x90}; /* UDS ReadDataByIdentifier VIN */
    CHECK_EQ(ep_isotp_send(&tester.tp, req, 3, 0), EP_OK);
    CHECK_EQ(b.q[0].data[0], 0x03);
    CHECK_EQ(b.q[0].data[4], 0xCC); /* padding */
    pump(&b, &tester, &ecu, 1000000);
    CHECK_EQ(ecu.rx_count, 1);
    CHECK_EQ(ecu.rx_len, 3);
    CHECK_MEM(ecu.rx, req, 3);
    CHECK_EQ(tester.tx_done, 1);
}

static void test_multi_frame_with_blocks(void)
{
    bus_t b = {0};
    node_t tester, ecu;
    node_init(&tester, &b, 0x7E0, 0x7E8, 0, 0, 4095);
    node_init(&ecu, &b, 0x7E8, 0x7E0, 4 /* BS */, 0, 4095);
    uint8_t vin[100];
    for (int i = 0; i < 100; i++)
        vin[i] = (uint8_t)(i * 7);
    CHECK_EQ(ep_isotp_send(&ecu.tp, vin, sizeof(vin), 0), EP_OK); /* ECU -> tester */
    CHECK_EQ(b.q[0].data[0], 0x10);
    CHECK_EQ(b.q[0].data[1], 100);
    CHECK_EQ(ep_isotp_send(&ecu.tp, vin, 10, 0), EP_ERR_BUSY);
    pump(&b, &tester, &ecu, 10000000);
    CHECK_EQ(tester.rx_count, 1);
    CHECK_EQ(tester.rx_len, 100);
    CHECK_MEM(tester.rx, vin, 100);
    CHECK_EQ(ecu.tx_done, 1);
    CHECK_EQ(ecu.tx_result, EP_OK);
}

static void test_block_size_and_stmin(void)
{
    bus_t b = {0};
    node_t tester, ecu;
    /* receiver (tester) asks for BS=2 and STmin=5 ms */
    node_init(&tester, &b, 0x7E0, 0x7E8, 2, 5, 4095);
    node_init(&ecu, &b, 0x7E8, 0x7E0, 0, 0, 4095);
    uint8_t data[6 + 7 * 5];
    memset(data, 0x5A, sizeof(data));
    ep_isotp_send(&ecu.tp, data, sizeof(data), 0);
    pump(&b, &tester, &ecu, 200000000);
    CHECK_EQ(tester.rx_len, sizeof(data));

    int fcs = 0;
    uint64_t last_cf = 0;
    bool spacing_ok = true;
    for (size_t i = 0; i < b.head; i++) {
        uint8_t pci = b.q[i].data[0] >> 4;
        if (pci == 3)
            fcs++;
        if (pci == 2) {
            if (last_cf && b.at[i] - last_cf < 5000000 && b.q[i - 1].data[0] >> 4 == 2)
                spacing_ok = false;
            last_cf = b.at[i];
        }
    }
    CHECK_EQ(fcs, 3); /* after FF, then after each block of 2 CFs (5 CFs total) */
    CHECK(spacing_ok);
    CHECK_EQ(ep_isotp_stmin_to_us(0xF3), 300);
    CHECK_EQ(ep_isotp_stmin_to_us(0x80), 127000);
}

static void test_lost_cf_aborts(void)
{
    bus_t b = {.drop_cf_index = 3};
    node_t tester, ecu;
    node_init(&tester, &b, 0x7E0, 0x7E8, 0, 0, 4095);
    node_init(&ecu, &b, 0x7E8, 0x7E0, 0, 0, 4095);
    uint8_t data[60] = {0};
    ep_isotp_send(&ecu.tp, data, sizeof(data), 0);
    pump(&b, &tester, &ecu, 5000000);
    CHECK_EQ(tester.rx_count, 0);
    CHECK(tester.tp.errors >= 1);
}

static void test_fc_timeout(void)
{
    bus_t b = {.drop_fc = true};
    node_t tester, ecu;
    node_init(&tester, &b, 0x7E0, 0x7E8, 0, 0, 4095);
    node_init(&ecu, &b, 0x7E8, 0x7E0, 0, 0, 4095);
    uint8_t data[20] = {0};
    ep_isotp_send(&ecu.tp, data, sizeof(data), 0);
    pump(&b, &tester, &ecu, 999000000);
    CHECK_EQ(ecu.tx_done, 0);
    pump(&b, &tester, &ecu, 1100000000); /* N_Bs = 1000 ms */
    CHECK_EQ(ecu.tx_done, 1);
    CHECK_EQ(ecu.tx_result, EP_ERR_TIMEOUT);
}

static void test_receiver_overflow(void)
{
    bus_t b = {0};
    node_t tester, ecu;
    node_init(&tester, &b, 0x7E0, 0x7E8, 0, 0, 32); /* small buffer */
    node_init(&ecu, &b, 0x7E8, 0x7E0, 0, 0, 4095);
    uint8_t data[64] = {0};
    ep_isotp_send(&ecu.tp, data, sizeof(data), 0);
    pump(&b, &tester, &ecu, 1000000);
    CHECK_EQ(ecu.tx_result, EP_ERR_OVERFLOW); /* FC.OVFLW */
    CHECK_EQ(tester.rx_count, 0);
}

int main(void)
{
    RUN(test_single_frame);
    RUN(test_multi_frame_with_blocks);
    RUN(test_block_size_and_stmin);
    RUN(test_lost_cf_aborts);
    RUN(test_fc_timeout);
    RUN(test_receiver_overflow);
    TEST_MAIN_END();
}
