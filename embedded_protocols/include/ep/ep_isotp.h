/*
 * ep_isotp.h - ISO 15765-2 (ISO-TP) transport over classic CAN.
 *
 * Carries messages up to 4095 bytes (UDS diagnostics, OBD-II, J1939-like
 * bulk data) over 8-byte CAN frames:
 *
 *   SF  0x0L d0..d6            single frame, L = 1..7
 *   FF  0x1L LL d0..d5         first frame, 12-bit length
 *   CF  0x2N d0..d6            consecutive frame, N = sequence (mod 16)
 *   FC  0x3S BS STmin          flow control: S = 0 CTS, 1 WAIT, 2 OVFLW
 *
 * Event driven and non-blocking: feed received frames with
 * ep_isotp_on_frame() and call ep_isotp_poll() periodically (>= STmin
 * resolution). One instance = one full-duplex channel (tx_id/rx_id pair).
 */
#ifndef EP_ISOTP_H
#define EP_ISOTP_H

#include "ep_can.h"

#ifdef __cplusplus
extern "C" {
#endif

#define EP_ISOTP_MAX_LEN 4095u

typedef enum { EP_ISOTP_IDLE = 0, EP_ISOTP_WAIT_FC, EP_ISOTP_SENDING, EP_ISOTP_RECEIVING } ep_isotp_state_t;

typedef struct ep_isotp ep_isotp_t;

typedef struct {
    uint32_t tx_id, rx_id;
    bool ext_id;
    uint8_t padding;      /* fill byte; frames are always padded to 8 (common OEM rule) */
    uint8_t block_size;   /* BS we advertise as receiver (0 = unlimited)  */
    uint8_t st_min;       /* STmin we advertise (raw encoding)            */
    uint32_t timeout_ms;  /* N_Bs / N_Cr, default 1000                     */
    uint8_t max_wft;      /* max FC.WAIT frames accepted                   */
    ep_status_t (*send)(void *arg, const ep_can_frame_t *f);
    void (*on_rx)(void *arg, const uint8_t *data, size_t len);
    void (*on_tx_done)(void *arg, ep_status_t result);
    void *arg;
    uint8_t *rx_buf;      /* reassembly buffer supplied by the user */
    size_t rx_cap;
} ep_isotp_config_t;

struct ep_isotp {
    ep_isotp_config_t cfg;
    /* transmitter */
    ep_isotp_state_t tx_state;
    const uint8_t *tx_buf;
    size_t tx_len, tx_off;
    uint8_t tx_sn, tx_bs, tx_bs_left, tx_wft;
    uint32_t tx_stmin_us;
    uint64_t tx_next_ns, tx_deadline_ns;
    /* receiver */
    ep_isotp_state_t rx_state;
    size_t rx_len, rx_off;
    uint8_t rx_sn, rx_bs_cnt;
    uint64_t rx_deadline_ns;
    uint32_t errors;
};

void ep_isotp_config_default(ep_isotp_config_t *cfg);
ep_status_t ep_isotp_init(ep_isotp_t *t, const ep_isotp_config_t *cfg);
/* Data must stay valid until on_tx_done. */
ep_status_t ep_isotp_send(ep_isotp_t *t, const uint8_t *data, size_t len, uint64_t now_ns);
void ep_isotp_on_frame(ep_isotp_t *t, const ep_can_frame_t *f, uint64_t now_ns);
void ep_isotp_poll(ep_isotp_t *t, uint64_t now_ns);

uint32_t ep_isotp_stmin_to_us(uint8_t raw);

#ifdef __cplusplus
}
#endif

#endif /* EP_ISOTP_H */
