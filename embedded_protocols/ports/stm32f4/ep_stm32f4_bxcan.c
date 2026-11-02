/*
 * STM32F4 bxCAN (CAN1) backend for ep_can_t.
 *
 *   ep_can_init(&can, &ep_stm32f4_can_ops, STM32_CAN1);
 *   ep_can_start(&can, 42000000, 500000, &ep_can_btc_bxcan, EP_CAN_MODE_NORMAL);
 *   nvic: CAN1_RX0_IRQHandler drains FIFO0 into the driver core.
 */
#include "ep/ep_can.h"
#include "ep_stm32f4_regs.h"

static ep_can_t *g_can1;

static ep_status_t enter_init(stm32_can_t *c)
{
    c->MCR = (c->MCR & ~CAN_MCR_SLEEP) | CAN_MCR_INRQ;
    for (uint32_t guard = 1000000; !(c->MSR & CAN_MSR_INAK); )
        if (!--guard) return EP_ERR_TIMEOUT;
    return EP_OK;
}

static ep_status_t leave_init(stm32_can_t *c)
{
    c->MCR &= ~CAN_MCR_INRQ;
    for (uint32_t guard = 1000000; c->MSR & CAN_MSR_INAK; )
        if (!--guard) return EP_ERR_TIMEOUT; /* needs 11 recessive bits: bus connected? */
    return EP_OK;
}

static ep_status_t c_bittiming(void *ctx, const ep_can_bittiming_t *bt, const ep_can_bittiming_t *data)
{
    stm32_can_t *c = ctx;
    if (data)
        return EP_ERR_UNSUPPORTED; /* bxCAN is classic CAN */
    ep_status_t st = enter_init(c);
    if (st != EP_OK)
        return st;
    c->MCR |= CAN_MCR_ABOM | CAN_MCR_TXFP; /* auto bus-off recovery, FIFO tx order */
    uint32_t keep = c->BTR & (CAN_BTR_LBKM | CAN_BTR_SILM);
    c->BTR = keep | ((uint32_t)(bt->sjw - 1u) << 24) | ((uint32_t)(bt->tseg2 - 1u) << 20) |
             ((uint32_t)(bt->tseg1 - 1u) << 16) | (bt->brp - 1u);
    return EP_OK;
}

static ep_status_t c_mode(void *ctx, ep_can_mode_t m)
{
    stm32_can_t *c = ctx;
    ep_status_t st = enter_init(c);
    if (st != EP_OK)
        return st;
    uint32_t btr = c->BTR & ~(CAN_BTR_LBKM | CAN_BTR_SILM);
    switch (m) {
    case EP_CAN_MODE_LOOPBACK: btr |= CAN_BTR_LBKM; break;
    case EP_CAN_MODE_LISTEN_ONLY: btr |= CAN_BTR_SILM; break;
    case EP_CAN_MODE_SLEEP: c->MCR |= CAN_MCR_SLEEP; return EP_OK;
    default: break;
    }
    c->BTR = btr;
    c->IER |= CAN_IER_FMPIE0;
    nvic_enable(IRQ_CAN1_RX0);
    return leave_init(c);
}

static ep_status_t c_send(void *ctx, const ep_can_frame_t *f)
{
    stm32_can_t *c = ctx;
    uint32_t tsr = c->TSR;
    int mb = -1;
    for (int i = 0; i < 3; i++)
        if (tsr & (CAN_TSR_TME0 << i)) {
            mb = i;
            break;
        }
    if (mb < 0)
        return EP_ERR_BUSY;
    stm32_can_txmb_t *t = &c->TX[mb];
    uint32_t tir = (f->flags & EP_CAN_FLAG_EXT) ? ((f->id << 3) | CAN_TIR_IDE) : (f->id << 21);
    if (f->flags & EP_CAN_FLAG_RTR)
        tir |= CAN_TIR_RTR;
    t->TDTR = f->len & 0x0Fu;
    t->TDLR = (uint32_t)f->data[0] | ((uint32_t)f->data[1] << 8) | ((uint32_t)f->data[2] << 16) |
              ((uint32_t)f->data[3] << 24);
    t->TDHR = (uint32_t)f->data[4] | ((uint32_t)f->data[5] << 8) | ((uint32_t)f->data[6] << 16) |
              ((uint32_t)f->data[7] << 24);
    t->TIR = tir | CAN_TIR_TXRQ; /* request last */
    return EP_OK;
}

static ep_status_t c_errors(void *ctx, uint16_t *tec, uint16_t *rec)
{
    stm32_can_t *c = ctx;
    uint32_t esr = c->ESR;
    *tec = (esr & CAN_ESR_BOFF) ? 256 : (uint16_t)((esr >> 16) & 0xFF);
    *rec = (uint16_t)((esr >> 24) & 0xFF);
    return EP_OK;
}

/* Hardware filters: 32-bit mask mode, one bank per filter, FIFO0. */
static ep_status_t c_filters(void *ctx, const ep_can_filter_t *f, size_t n)
{
    stm32_can_t *c = ctx;
    if (n > 14)
        return EP_ERR_OVERFLOW;
    c->FMR |= CAN_FMR_FINIT;
    c->FA1R = 0;
    c->FM1R = 0;            /* mask mode  */
    c->FS1R = 0x0FFFFFFFu;  /* 32-bit     */
    c->FFA1R = 0;           /* FIFO0      */
    if (n == 0) {           /* accept all */
        c->FB[0].FR1 = 0;
        c->FB[0].FR2 = 0;
        c->FA1R = 1;
    }
    for (size_t i = 0; i < n; i++) {
        uint32_t id, mask;
        if (f[i].ext) {
            id = (f[i].id << 3) | CAN_TIR_IDE;
            mask = (f[i].mask << 3) | CAN_TIR_IDE;
        } else {
            id = f[i].id << 21;
            mask = (f[i].mask << 21) | CAN_TIR_IDE;
        }
        c->FB[i].FR1 = id;
        c->FB[i].FR2 = mask;
        c->FA1R |= 1u << i;
    }
    c->FMR &= ~CAN_FMR_FINIT;
    return EP_OK;
}

const ep_can_ops_t ep_stm32f4_can_ops = {
    .set_bittiming = c_bittiming,
    .set_mode = c_mode,
    .send = c_send,
    .get_error_counters = c_errors,
    .set_hw_filters = c_filters,
};

void ep_stm32f4_can_attach(ep_can_t *can);
void ep_stm32f4_can_attach(ep_can_t *can)
{
    g_can1 = can;
}

void CAN1_RX0_IRQHandler(void);
void CAN1_RX0_IRQHandler(void)
{
    stm32_can_t *c = STM32_CAN1;
    while (c->RF0R & CAN_RF0R_FMP0) {
        stm32_can_rxfifo_t *r = &c->RX[0];
        ep_can_frame_t f = {0};
        uint32_t rir = r->RIR;
        if (rir & CAN_TIR_IDE) {
            f.flags |= EP_CAN_FLAG_EXT;
            f.id = rir >> 3;
        } else {
            f.id = rir >> 21;
        }
        if (rir & CAN_TIR_RTR)
            f.flags |= EP_CAN_FLAG_RTR;
        f.len = (uint8_t)(r->RDTR & 0x0F);
        if (f.len > 8)
            f.len = 8;
        uint32_t lo = r->RDLR, hi = r->RDHR;
        for (int i = 0; i < 4; i++) {
            f.data[i] = (uint8_t)(lo >> (8 * i));
            f.data[4 + i] = (uint8_t)(hi >> (8 * i));
        }
        c->RF0R |= CAN_RF0R_RFOM0; /* release FIFO slot */
        if (g_can1)
            ep_can_isr_rx(g_can1, &f);
    }
}
