/* SPDX-License-Identifier: MIT */
#ifndef TCU_ECALL_SM_H
#define TCU_ECALL_SM_H

#include <stdbool.h>
#include <stdint.h>

/*
 * eCall session state machine (IVS side, after EN 16062 / UNECE R144).
 *
 * The machine is pure: all side effects go through ecall_ops so it can be
 * unit-tested on the host and driven by ecalld on target.
 */

typedef enum {
	ECALL_IDLE,
	ECALL_CANCEL_WINDOW, /* manual trigger only: occupant may still cancel */
	ECALL_DIALING,       /* call setup or waiting to redial */
	ECALL_MSD_TX,        /* call connected, MSD sent, waiting for PSAP ack */
	ECALL_VOICE,         /* voice channel open to PSAP */
	ECALL_CALLBACK_WAIT, /* call cleared; stay registered for PSAP call-back */
	ECALL_STATE_COUNT
} ecall_state_t;

typedef enum {
	ECALL_EV_TRIGGER_MANUAL,
	ECALL_EV_TRIGGER_AUTO,   /* airbag / crash sensor */
	ECALL_EV_CANCEL,
	ECALL_EV_TIMER,
	ECALL_EV_CALL_CONNECTED,
	ECALL_EV_CALL_FAILED,
	ECALL_EV_MSD_ACK,
	ECALL_EV_MSD_REQUEST,    /* PSAP pulls a fresh MSD */
	ECALL_EV_CALL_ENDED,
	ECALL_EV_INCOMING_CALL,
} ecall_event_t;

struct ecall_ops {
	void (*dial)(void *ctx, bool automatic, bool test);
	void (*send_msd)(void *ctx, uint8_t message_id);
	void (*answer)(void *ctx);
	void (*hangup)(void *ctx);
	void (*start_timer)(void *ctx, uint32_t ms);
	void (*stop_timer)(void *ctx);
	void (*audio_route)(void *ctx, bool to_psap);
	void (*notify)(void *ctx, ecall_state_t s, const char *reason);
};

struct ecall_cfg {
	uint32_t cancel_window_ms;  /* e.g. 3 s */
	uint32_t redial_delay_ms;   /* e.g. 5 s */
	uint32_t max_dial_attempts; /* e.g. 10 */
	uint32_t msd_ack_timeout_ms;/* e.g. 20 s; voice opens even without ack */
	uint32_t callback_window_ms;/* e.g. 60 min registered after clear-down */
	bool test_call;
};

struct ecall_sm {
	ecall_state_t state;
	struct ecall_cfg cfg;
	const struct ecall_ops *ops;
	void *ctx;
	bool automatic;
	bool msd_acked;
	bool redial_pending;
	uint32_t dial_attempts;
	uint8_t msd_id;
};

void ecall_sm_init(struct ecall_sm *sm, const struct ecall_cfg *cfg,
		   const struct ecall_ops *ops, void *ctx);
ecall_state_t ecall_sm_handle(struct ecall_sm *sm, ecall_event_t ev);
const char *ecall_state_name(ecall_state_t s);

#endif
