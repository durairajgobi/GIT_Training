#include "ep/ep_can.h"
#include "ep_test.h"

static void test_dlc(void)
{
    CHECK_EQ(ep_can_len_to_dlc(8), 8);
    CHECK_EQ(ep_can_len_to_dlc(9), 9);   /* rounds up to 12 */
    CHECK_EQ(ep_can_dlc_to_len(9), 12);
    CHECK_EQ(ep_can_len_to_dlc(33), 14); /* -> 48 */
    CHECK_EQ(ep_can_len_to_dlc(64), 15);
    CHECK_EQ(ep_can_dlc_to_len(15), 64);
}

static void test_frame_validation(void)
{
    ep_can_frame_t f = {.id = 0x7FF, .len = 8};
    CHECK(ep_can_frame_valid(&f));
    f.id = 0x800;
    CHECK(!ep_can_frame_valid(&f)); /* > 11 bit without EXT */
    f.flags = EP_CAN_FLAG_EXT;
    CHECK(ep_can_frame_valid(&f));
    f.len = 12;
    CHECK(!ep_can_frame_valid(&f)); /* classic max 8 */
    f.flags |= EP_CAN_FLAG_FD | EP_CAN_FLAG_BRS;
    CHECK(ep_can_frame_valid(&f));
    f.len = 13;
    CHECK(!ep_can_frame_valid(&f)); /* not a valid FD length */
    f.len = 0;
    f.flags = EP_CAN_FLAG_FD | EP_CAN_FLAG_RTR;
    CHECK(!ep_can_frame_valid(&f)); /* no RTR in FD */
}

static void test_bittiming(void)
{
    ep_can_bittiming_t bt;
    /* STM32F1 bxCAN, APB1 = 36 MHz, 500 kbit/s */
    CHECK_EQ(ep_can_calc_bittiming(36000000, 500000, 0, &ep_can_btc_bxcan, &bt), EP_OK);
    CHECK_EQ(bt.bitrate, 500000);
    CHECK_EQ(bt.sample_point_permille, 875);
    CHECK_EQ(bt.brp * (1u + bt.tseg1 + bt.tseg2), 72);
    CHECK_EQ(bt.brp, 9);
    CHECK_EQ(bt.tseg1, 6);
    CHECK_EQ(bt.tseg2, 1);
    CHECK_EQ(bt.sjw, 1);

    /* MCP2515, 8 MHz crystal (clock = Fosc/2), 125 kbit/s */
    CHECK_EQ(ep_can_calc_bittiming(4000000, 125000, 0, &ep_can_btc_mcp2515, &bt), EP_OK);
    CHECK_EQ(bt.brp, 2);
    CHECK_EQ(1 + bt.tseg1 + bt.tseg2, 16);
    CHECK_EQ(bt.sample_point_permille, 875);

    /* M_CAN 80 MHz, 1 Mbit/s nominal -> CiA 75 % sample point */
    CHECK_EQ(ep_can_calc_bittiming(80000000, 1000000, 0, &ep_can_btc_mcan_nominal, &bt), EP_OK);
    CHECK_EQ(bt.brp, 1);
    CHECK_EQ(bt.tseg1, 59);
    CHECK_EQ(bt.tseg2, 20);
    CHECK_EQ(bt.sample_point_permille, 750);

    /* 7 MHz -> 7 tq at 1 Mbit/s is legal on bxCAN ... */
    CHECK_EQ(ep_can_calc_bittiming(7000000, 1000000, 0, &ep_can_btc_bxcan, &bt), EP_OK);
    CHECK_EQ(1 + bt.tseg1 + bt.tseg2, 7);
    /* ... but 2 MHz leaves only 2 tq per bit (minimum is 3) */
    CHECK_EQ(ep_can_calc_bittiming(2000000, 1000000, 0, &ep_can_btc_bxcan, &bt), EP_ERR_UNSUPPORTED);
}

static void test_filters(void)
{
    ep_can_filter_t f[2] = {
        {.id = 0x100, .mask = 0x7F0, .ext = false},      /* 0x100..0x10F */
        {.id = 0x18DAF110, .mask = 0x1FFFFFFF, .ext = true},
    };
    ep_can_frame_t fr = {.id = 0x10A};
    CHECK(ep_can_filter_match(f, 2, &fr));
    fr.id = 0x110;
    CHECK(!ep_can_filter_match(f, 2, &fr));
    fr.id = 0x18DAF110;
    fr.flags = EP_CAN_FLAG_EXT;
    CHECK(ep_can_filter_match(f, 2, &fr));
    fr.id = 0x10A; /* same bits but extended: no match */
    CHECK(!ep_can_filter_match(f, 2, &fr));
    CHECK(ep_can_filter_match(f, 0, &fr)); /* no filters = accept all */
}

static void test_error_states(void)
{
    CHECK_EQ(ep_can_error_state(0, 0), EP_CAN_ERROR_ACTIVE);
    CHECK_EQ(ep_can_error_state(96, 0), EP_CAN_ERROR_WARNING);
    CHECK_EQ(ep_can_error_state(0, 127), EP_CAN_ERROR_WARNING);
    CHECK_EQ(ep_can_error_state(128, 0), EP_CAN_ERROR_PASSIVE);
    CHECK_EQ(ep_can_error_state(0, 200), EP_CAN_ERROR_PASSIVE);
    CHECK_EQ(ep_can_error_state(256, 0), EP_CAN_BUS_OFF);
}

/* -------- driver core over a loopback mock controller -------- */
typedef struct {
    ep_can_t *can;
    int mailboxes;
    uint16_t tec, rec;
    ep_can_mode_t mode;
    ep_can_bittiming_t bt;
} mock_ctrl_t;

static ep_status_t m_bt(void *ctx, const ep_can_bittiming_t *n, const ep_can_bittiming_t *d)
{
    (void)d;
    ((mock_ctrl_t *)ctx)->bt = *n;
    return EP_OK;
}
static ep_status_t m_mode(void *ctx, ep_can_mode_t m)
{
    ((mock_ctrl_t *)ctx)->mode = m;
    return EP_OK;
}
static ep_status_t m_send(void *ctx, const ep_can_frame_t *f)
{
    mock_ctrl_t *m = ctx;
    if (m->mailboxes == 0)
        return EP_ERR_BUSY;
    m->mailboxes--;
    if (m->mode == EP_CAN_MODE_LOOPBACK)
        ep_can_isr_rx(m->can, f); /* frame comes back through the RX ISR */
    return EP_OK;
}
static ep_status_t m_err(void *ctx, uint16_t *tec, uint16_t *rec)
{
    mock_ctrl_t *m = ctx;
    *tec = m->tec;
    *rec = m->rec;
    return EP_OK;
}
static const ep_can_ops_t mock_ops = {m_bt, m_mode, m_send, m_err, NULL};

static void test_driver_loopback(void)
{
    ep_can_t can;
    mock_ctrl_t m = {.can = &can, .mailboxes = 3};
    CHECK_EQ(ep_can_init(&can, &mock_ops, &m), EP_OK);
    CHECK_EQ(ep_can_start(&can, 36000000, 500000, &ep_can_btc_bxcan, EP_CAN_MODE_LOOPBACK), EP_OK);
    CHECK_EQ(m.bt.brp, 9);

    ep_can_filter_t flt = {.id = 0x123, .mask = 0x7FF};
    ep_can_set_filters(&can, &flt, 1);

    ep_can_frame_t tx = {.id = 0x123, .len = 2, .data = {0xBE, 0xEF}};
    ep_can_frame_t other = {.id = 0x124, .len = 1};
    CHECK_EQ(ep_can_send(&can, &tx), EP_OK);
    CHECK_EQ(ep_can_send(&can, &other), EP_OK);
    CHECK_EQ(ep_can_send(&can, &tx), EP_OK);
    CHECK_EQ(ep_can_send(&can, &tx), EP_ERR_BUSY); /* all mailboxes pending */
    CHECK_EQ(can.stats.tx_busy, 1);
    CHECK_EQ(can.stats.rx_filtered, 1);

    ep_can_frame_t rx;
    CHECK_EQ(ep_can_receive(&can, &rx), EP_OK);
    CHECK_EQ(rx.id, 0x123);
    CHECK_EQ(rx.data[1], 0xEF);
    CHECK_EQ(ep_can_receive(&can, &rx), EP_OK);
    CHECK_EQ(ep_can_receive(&can, &rx), EP_ERR_AGAIN);

    m.tec = 256;
    CHECK_EQ(ep_can_update_state(&can), EP_CAN_BUS_OFF);
    m.mailboxes = 3;
    CHECK_EQ(ep_can_send(&can, &tx), EP_ERR_BUS_OFF);
    CHECK_EQ(can.stats.bus_off_events, 1);
}

static void test_rx_queue_overflow(void)
{
    ep_can_t can;
    mock_ctrl_t m = {.can = &can};
    ep_can_init(&can, &mock_ops, &m);
    ep_can_frame_t f = {.id = 1, .len = 0};
    for (unsigned i = 0; i < EP_CAN_RXQ_LEN + 5; i++)
        ep_can_isr_rx(&can, &f);
    CHECK_EQ(can.stats.rx_frames, EP_CAN_RXQ_LEN);
    CHECK_EQ(can.stats.rx_overflow, 5);
}

int main(void)
{
    RUN(test_dlc);
    RUN(test_frame_validation);
    RUN(test_bittiming);
    RUN(test_filters);
    RUN(test_error_states);
    RUN(test_driver_loopback);
    RUN(test_rx_queue_overflow);
    TEST_MAIN_END();
}
