/*
 * ep_can.h - CAN 2.0A/B and CAN FD: frames, bit timing, filters, error
 * state, and a controller-independent driver core with an ISR rx queue.
 */
#ifndef EP_CAN_H
#define EP_CAN_H

#include "ep_common.h"

#ifdef __cplusplus
extern "C" {
#endif

#define EP_CAN_FLAG_EXT 0x01u /* 29-bit identifier            */
#define EP_CAN_FLAG_RTR 0x02u /* remote frame (classic only)  */
#define EP_CAN_FLAG_FD  0x04u /* CAN FD frame                 */
#define EP_CAN_FLAG_BRS 0x08u /* FD bit-rate switch           */
#define EP_CAN_FLAG_ESI 0x10u /* FD error state indicator     */

#define EP_CAN_SFF_MASK 0x000007FFu
#define EP_CAN_EFF_MASK 0x1FFFFFFFu
#define EP_CAN_MAX_DLEN 8u
#define EP_CANFD_MAX_DLEN 64u

typedef struct {
    uint32_t id;
    uint8_t len; /* payload length in bytes (0..8 / 0..64) */
    uint8_t flags;
    uint8_t data[EP_CANFD_MAX_DLEN];
    uint64_t timestamp_ns;
} ep_can_frame_t;

uint8_t ep_can_len_to_dlc(uint8_t len); /* rounds up to next valid FD size */
uint8_t ep_can_dlc_to_len(uint8_t dlc);
bool ep_can_frame_valid(const ep_can_frame_t *f);

/* ----------------------------- Bit timing -------------------------- */
/*  |SYNC|  PROP + PHASE1 (tseg1)  | PHASE2 (tseg2) |
 *    1tq                          ^ sample point                       */
typedef struct {
    uint32_t brp;
    uint16_t tseg1;
    uint16_t tseg2;
    uint16_t sjw;
    uint32_t bitrate;             /* achieved */
    uint16_t sample_point_permille;
} ep_can_bittiming_t;

typedef struct {
    uint16_t tseg1_min, tseg1_max;
    uint16_t tseg2_min, tseg2_max;
    uint16_t sjw_max;
    uint32_t brp_min, brp_max;
} ep_can_bittiming_const_t;

/* Limits of a few common controllers. */
extern const ep_can_bittiming_const_t ep_can_btc_bxcan;   /* STM32 bxCAN     */
extern const ep_can_bittiming_const_t ep_can_btc_mcp2515; /* Microchip       */
extern const ep_can_bittiming_const_t ep_can_btc_mcan_nominal; /* Bosch M_CAN */

/* sample_point_permille == 0 selects CiA 301 defaults (87.5 %, 80 % >800k).
 * Picks the exact-bitrate solution with the smallest sample point error,
 * preferring the most time quanta per bit. */
ep_status_t ep_can_calc_bittiming(uint32_t clock_hz, uint32_t bitrate,
                                  uint16_t sample_point_permille,
                                  const ep_can_bittiming_const_t *btc,
                                  ep_can_bittiming_t *out);

/* ----------------------------- Filters ----------------------------- */
typedef struct {
    uint32_t id;
    uint32_t mask;     /* 1 bits must match */
    bool ext;          /* match EXT frames only (else STD only) */
} ep_can_filter_t;

bool ep_can_filter_match(const ep_can_filter_t *f, size_t n, const ep_can_frame_t *frame);

/* --------------------- Fault confinement (ISO 11898-1) ------------- */
typedef enum {
    EP_CAN_ERROR_ACTIVE = 0,
    EP_CAN_ERROR_WARNING, /* TEC or REC >= 96 */
    EP_CAN_ERROR_PASSIVE, /* TEC or REC >= 128 */
    EP_CAN_BUS_OFF        /* TEC >= 256 */
} ep_can_state_t;

ep_can_state_t ep_can_error_state(uint16_t tec, uint16_t rec);

/* -------------------------- Driver core ---------------------------- */
typedef enum {
    EP_CAN_MODE_NORMAL = 0,
    EP_CAN_MODE_LOOPBACK,
    EP_CAN_MODE_LISTEN_ONLY,
    EP_CAN_MODE_SLEEP
} ep_can_mode_t;

typedef struct {
    ep_status_t (*set_bittiming)(void *ctx, const ep_can_bittiming_t *nominal,
                                 const ep_can_bittiming_t *data /* FD, may be NULL */);
    ep_status_t (*set_mode)(void *ctx, ep_can_mode_t mode);
    ep_status_t (*send)(void *ctx, const ep_can_frame_t *f); /* EP_ERR_BUSY: no free mailbox */
    ep_status_t (*get_error_counters)(void *ctx, uint16_t *tec, uint16_t *rec);
    ep_status_t (*set_hw_filters)(void *ctx, const ep_can_filter_t *f, size_t n); /* optional */
} ep_can_ops_t;

#ifndef EP_CAN_RXQ_LEN
#define EP_CAN_RXQ_LEN 32u /* power of two */
#endif
#define EP_CAN_MAX_SW_FILTERS 8u

typedef struct {
    uint32_t rx_frames, tx_frames, rx_overflow, rx_filtered, tx_busy, bus_off_events;
} ep_can_stats_t;

typedef struct {
    const ep_can_ops_t *ops;
    void *ctx;
    ep_can_frame_t rxq[EP_CAN_RXQ_LEN];
    size_t rx_head, rx_tail;
    ep_can_filter_t filters[EP_CAN_MAX_SW_FILTERS];
    size_t n_filters;
    ep_can_state_t state;
    ep_can_stats_t stats;
} ep_can_t;

ep_status_t ep_can_init(ep_can_t *can, const ep_can_ops_t *ops, void *ctx);
ep_status_t ep_can_start(ep_can_t *can, uint32_t clock_hz, uint32_t bitrate,
                         const ep_can_bittiming_const_t *btc, ep_can_mode_t mode);
ep_status_t ep_can_set_filters(ep_can_t *can, const ep_can_filter_t *f, size_t n);
ep_status_t ep_can_send(ep_can_t *can, const ep_can_frame_t *f);
ep_status_t ep_can_receive(ep_can_t *can, ep_can_frame_t *f);  /* EP_ERR_AGAIN if empty */
ep_can_state_t ep_can_update_state(ep_can_t *can);

/* Called by the port's RX interrupt (FIFO pending) for each frame. */
void ep_can_isr_rx(ep_can_t *can, const ep_can_frame_t *f);

#ifdef __cplusplus
}
#endif

#endif /* EP_CAN_H */
