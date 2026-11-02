#include "ep/ep_can.h"

#include <string.h>

static const uint8_t dlc2len[16] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 12, 16, 20, 24, 32, 48, 64};

uint8_t ep_can_dlc_to_len(uint8_t dlc)
{
    return dlc2len[dlc & 0x0F];
}

uint8_t ep_can_len_to_dlc(uint8_t len)
{
    for (uint8_t dlc = 0; dlc < 16; dlc++)
        if (dlc2len[dlc] >= len)
            return dlc;
    return 15;
}

bool ep_can_frame_valid(const ep_can_frame_t *f)
{
    uint32_t idmask = (f->flags & EP_CAN_FLAG_EXT) ? EP_CAN_EFF_MASK : EP_CAN_SFF_MASK;
    if (f->id & ~idmask)
        return false;
    if (f->flags & EP_CAN_FLAG_FD) {
        if (f->flags & EP_CAN_FLAG_RTR)
            return false; /* no remote frames in FD */
        return f->len <= 64 && dlc2len[ep_can_len_to_dlc(f->len)] == f->len;
    }
    if (f->flags & (EP_CAN_FLAG_BRS | EP_CAN_FLAG_ESI))
        return false;
    return f->len <= 8;
}

/* --------------------------- Bit timing ---------------------------- */

const ep_can_bittiming_const_t ep_can_btc_bxcan = {1, 16, 1, 8, 4, 1, 1024};
/* MCP2515: Tq = 2*(BRP+1)/Fosc -> pass clock_hz = Fosc/2. PRSEG+PS1 2..16. */
const ep_can_bittiming_const_t ep_can_btc_mcp2515 = {2, 16, 2, 8, 4, 1, 64};
const ep_can_bittiming_const_t ep_can_btc_mcan_nominal = {2, 256, 2, 128, 128, 1, 512};

static uint16_t default_sp(uint32_t bitrate)
{
    if (bitrate > 800000)
        return 750;
    if (bitrate > 500000)
        return 800;
    return 875;
}

ep_status_t ep_can_calc_bittiming(uint32_t clock_hz, uint32_t bitrate,
                                  uint16_t sp_permille,
                                  const ep_can_bittiming_const_t *btc,
                                  ep_can_bittiming_t *out)
{
    if (!clock_hz || !bitrate || !btc || !out || sp_permille >= 1000)
        return EP_ERR_PARAM;
    if (!sp_permille)
        sp_permille = default_sp(bitrate);

    bool found = false;
    uint32_t best_sp_err = UINT32_MAX;
    ep_can_bittiming_t best = {0};

    const uint32_t tq_min = 1u + btc->tseg1_min + btc->tseg2_min;
    const uint32_t tq_max = 1u + btc->tseg1_max + btc->tseg2_max;

    for (uint32_t brp = btc->brp_min; brp <= btc->brp_max; brp++) {
        uint64_t div = (uint64_t)brp * bitrate;
        if (clock_hz % div)
            continue; /* only exact bitrates: CAN tolerates ~0.5 % total */
        uint32_t ntq = (uint32_t)(clock_hz / div);
        if (ntq < tq_min)
            break; /* larger brp only makes it worse */
        if (ntq > tq_max)
            continue;

        /* tseg2 = ntq * (1 - sp), rounded */
        int32_t tseg2 = (int32_t)((ntq * (1000u - sp_permille) + 500u) / 1000u);
        if (tseg2 < btc->tseg2_min) tseg2 = btc->tseg2_min;
        if (tseg2 > btc->tseg2_max) tseg2 = btc->tseg2_max;
        int32_t tseg1 = (int32_t)ntq - 1 - tseg2;
        if (tseg1 > btc->tseg1_max) {
            tseg1 = btc->tseg1_max;
            tseg2 = (int32_t)ntq - 1 - tseg1;
        }
        if (tseg1 < btc->tseg1_min) {
            tseg1 = btc->tseg1_min;
            tseg2 = (int32_t)ntq - 1 - tseg1;
        }
        if (tseg2 < btc->tseg2_min || tseg2 > btc->tseg2_max)
            continue;

        uint32_t sp = (uint32_t)(1000u * (1u + (uint32_t)tseg1) / ntq);
        uint32_t err = sp > sp_permille ? sp - sp_permille : sp_permille - sp;
        if (!found || err < best_sp_err) {
            found = true;
            best_sp_err = err;
            best.brp = brp;
            best.tseg1 = (uint16_t)tseg1;
            best.tseg2 = (uint16_t)tseg2;
            best.sjw = (uint16_t)EP_MIN((uint32_t)tseg2, (uint32_t)btc->sjw_max);
            best.bitrate = (uint32_t)(clock_hz / ((uint64_t)brp * ntq));
            best.sample_point_permille = (uint16_t)sp;
            if (err == 0)
                break;
        }
    }
    if (!found)
        return EP_ERR_UNSUPPORTED;
    *out = best;
    return EP_OK;
}

/* ----------------------------- Filters ----------------------------- */

bool ep_can_filter_match(const ep_can_filter_t *f, size_t n, const ep_can_frame_t *frame)
{
    if (n == 0)
        return true; /* no filters: accept all */
    bool ext = (frame->flags & EP_CAN_FLAG_EXT) != 0;
    for (size_t i = 0; i < n; i++)
        if (f[i].ext == ext && ((frame->id ^ f[i].id) & f[i].mask) == 0)
            return true;
    return false;
}

ep_can_state_t ep_can_error_state(uint16_t tec, uint16_t rec)
{
    if (tec >= 256)
        return EP_CAN_BUS_OFF;
    if (tec >= 128 || rec >= 128)
        return EP_CAN_ERROR_PASSIVE;
    if (tec >= 96 || rec >= 96)
        return EP_CAN_ERROR_WARNING;
    return EP_CAN_ERROR_ACTIVE;
}

/* --------------------------- Driver core --------------------------- */

ep_status_t ep_can_init(ep_can_t *can, const ep_can_ops_t *ops, void *ctx)
{
    if (!can || !ops || !ops->send)
        return EP_ERR_PARAM;
    memset(can, 0, sizeof(*can));
    can->ops = ops;
    can->ctx = ctx;
    return EP_OK;
}

ep_status_t ep_can_start(ep_can_t *can, uint32_t clock_hz, uint32_t bitrate,
                         const ep_can_bittiming_const_t *btc, ep_can_mode_t mode)
{
    ep_can_bittiming_t bt;
    ep_status_t st = ep_can_calc_bittiming(clock_hz, bitrate, 0, btc, &bt);
    if (st != EP_OK)
        return st;
    if (can->ops->set_bittiming && (st = can->ops->set_bittiming(can->ctx, &bt, NULL)) != EP_OK)
        return st;
    if (can->ops->set_mode)
        st = can->ops->set_mode(can->ctx, mode);
    return st;
}

ep_status_t ep_can_set_filters(ep_can_t *can, const ep_can_filter_t *f, size_t n)
{
    if (n > EP_CAN_MAX_SW_FILTERS)
        return EP_ERR_OVERFLOW;
    /* prefer hardware acceptance filtering, keep the software copy as a
     * second stage (hardware banks are often coarser than requested) */
    if (can->ops->set_hw_filters)
        can->ops->set_hw_filters(can->ctx, f, n);
    memcpy(can->filters, f, n * sizeof(*f));
    can->n_filters = n;
    return EP_OK;
}

ep_status_t ep_can_send(ep_can_t *can, const ep_can_frame_t *f)
{
    if (!ep_can_frame_valid(f))
        return EP_ERR_PARAM;
    if (can->state == EP_CAN_BUS_OFF)
        return EP_ERR_BUS_OFF;
    ep_status_t st = can->ops->send(can->ctx, f);
    if (st == EP_OK)
        can->stats.tx_frames++;
    else if (st == EP_ERR_BUSY)
        can->stats.tx_busy++;
    return st;
}

void ep_can_isr_rx(ep_can_t *can, const ep_can_frame_t *f)
{
    if (!ep_can_filter_match(can->filters, can->n_filters, f)) {
        can->stats.rx_filtered++;
        return;
    }
    size_t head = can->rx_head;
    if (head - EP_LOAD_ACQUIRE(&can->rx_tail) >= EP_CAN_RXQ_LEN) {
        can->stats.rx_overflow++;
        return;
    }
    can->rxq[head % EP_CAN_RXQ_LEN] = *f;
    EP_STORE_RELEASE(&can->rx_head, head + 1);
    can->stats.rx_frames++;
}

ep_status_t ep_can_receive(ep_can_t *can, ep_can_frame_t *f)
{
    size_t tail = can->rx_tail;
    if (tail == EP_LOAD_ACQUIRE(&can->rx_head))
        return EP_ERR_AGAIN;
    *f = can->rxq[tail % EP_CAN_RXQ_LEN];
    EP_STORE_RELEASE(&can->rx_tail, tail + 1);
    return EP_OK;
}

ep_can_state_t ep_can_update_state(ep_can_t *can)
{
    uint16_t tec = 0, rec = 0;
    if (can->ops->get_error_counters && can->ops->get_error_counters(can->ctx, &tec, &rec) == EP_OK) {
        ep_can_state_t s = ep_can_error_state(tec, rec);
        if (s == EP_CAN_BUS_OFF && can->state != EP_CAN_BUS_OFF)
            can->stats.bus_off_events++;
        can->state = s;
    }
    return can->state;
}
