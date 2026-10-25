/* SPDX-License-Identifier: MIT */
#ifndef TCU_BT_RECONNECT_H
#define TCU_BT_RECONNECT_H

#include <stdbool.h>
#include <stdint.h>

#include "backoff.h"

/*
 * Per-phone reconnection policy on top of BlueZ Device1.Connect().
 *
 * Field issues this replaces the naive "retry every N seconds" loop for:
 *  1. Page collision: the phone connects to us while we page it; the late
 *     InProgress/AlreadyConnected error used to tear the link down.
 *  2. Phone deleted the pairing: retrying with a stale link key triggered a
 *     pairing prompt on the phone every few seconds. Now we stop and ask the
 *     driver to re-pair.
 *  3. Driver disconnected on the phone: auto-reconnect fought the user.
 *     Now we wait for the next ignition cycle.
 *  4. bluetoothd restart / adapter reset: state was lost. Now we wait for
 *     the adapter and resume.
 *  5. Unbounded retries drained battery in standby: retries are capped.
 */

typedef enum {
	BTR_IDLE,          /* no target / gave up until next ignition */
	BTR_WAIT_ADAPTER,
	BTR_PAGING,        /* Connect() in flight */
	BTR_CONNECTED,
	BTR_BACKOFF,
	BTR_NEEDS_REPAIR,  /* link key rejected; HMI shows "pair again" */
	BTR_SUSPENDED,     /* user disconnected or eCall active */
	BTR_STATE_COUNT
} btr_state_t;

typedef enum {
	BTR_EV_IGNITION_ON,
	BTR_EV_IGNITION_OFF,
	BTR_EV_ADAPTER_ON,
	BTR_EV_ADAPTER_OFF,
	BTR_EV_CONNECT_OK,
	BTR_EV_CONNECT_FAIL,
	BTR_EV_CONNECTED,     /* Device1.Connected -> true (either side) */
	BTR_EV_DISCONNECTED,  /* Device1.Connected -> false */
	BTR_EV_TIMER,
	BTR_EV_SUSPEND,       /* eCall started */
	BTR_EV_RESUME,
	BTR_EV_PAIRED,        /* re-pair finished */
} btr_event_t;

typedef enum {
	BTR_ERR_NONE,
	BTR_ERR_PAGE_TIMEOUT, /* phone out of range / BT off */
	BTR_ERR_IN_PROGRESS,  /* org.bluez.Error.InProgress / AlreadyConnected */
	BTR_ERR_AUTH,         /* AuthenticationFailed / key missing */
	BTR_ERR_BUSY,         /* controller busy, e.g. inquiry */
	BTR_ERR_OTHER,
} btr_error_t;

typedef enum {
	BTR_DISC_LINK_LOSS,   /* supervision timeout */
	BTR_DISC_REMOTE_USER, /* user disconnected on the phone */
	BTR_DISC_LOCAL,
} btr_disc_reason_t;

struct btr_ops {
	void (*connect)(void *ctx);
	void (*cancel)(void *ctx);
	void (*start_timer)(void *ctx, uint32_t ms);
	void (*notify)(void *ctx, btr_state_t s, const char *why);
};

struct btr_cfg {
	uint32_t link_loss_first_ms; /* quick first retry after link loss */
	uint32_t base_ms, max_ms;
	uint32_t max_attempts;       /* per ignition cycle */
};

struct btr {
	btr_state_t state;
	struct btr_cfg cfg;
	const struct btr_ops *ops;
	void *ctx;
	backoff_t backoff;
	uint32_t attempts;
	bool adapter_on;
	bool ignition;
	bool suspended_by_ecall;
	btr_state_t before_suspend;
};

void btr_init(struct btr *b, const struct btr_cfg *cfg,
	      const struct btr_ops *ops, void *ctx);
/* `arg` carries btr_error_t for CONNECT_FAIL, btr_disc_reason_t for DISCONNECTED. */
btr_state_t btr_handle(struct btr *b, btr_event_t ev, int arg);
const char *btr_state_name(btr_state_t s);

#endif
