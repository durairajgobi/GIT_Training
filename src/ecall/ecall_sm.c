// SPDX-License-Identifier: MIT
#include "ecall_sm.h"

#include <stddef.h>

static const char *const names[ECALL_STATE_COUNT] = {
	[ECALL_IDLE] = "IDLE",
	[ECALL_CANCEL_WINDOW] = "CANCEL_WINDOW",
	[ECALL_DIALING] = "DIALING",
	[ECALL_MSD_TX] = "MSD_TX",
	[ECALL_VOICE] = "VOICE",
	[ECALL_CALLBACK_WAIT] = "CALLBACK_WAIT",
};

const char *ecall_state_name(ecall_state_t s)
{
	return s < ECALL_STATE_COUNT ? names[s] : "?";
}

#define CALL(sm, fn, ...) do { if ((sm)->ops->fn) (sm)->ops->fn((sm)->ctx, ##__VA_ARGS__); } while (0)

static void enter(struct ecall_sm *sm, ecall_state_t s, const char *why)
{
	sm->state = s;
	CALL(sm, notify, s, why);
}

static void dial(struct ecall_sm *sm)
{
	sm->redial_pending = false;
	sm->dial_attempts++;
	CALL(sm, dial, sm->automatic, sm->cfg.test_call);
}

static void start_session(struct ecall_sm *sm, bool automatic)
{
	sm->automatic = automatic;
	sm->msd_acked = false;
	sm->dial_attempts = 0;
	CALL(sm, stop_timer);
	enter(sm, ECALL_DIALING, automatic ? "auto trigger" : "manual trigger");
	dial(sm);
}

/* A failed or dropped call is redialled until the attempt budget is spent. */
static void schedule_redial(struct ecall_sm *sm, const char *why)
{
	CALL(sm, audio_route, false);
	if (sm->dial_attempts >= sm->cfg.max_dial_attempts) {
		CALL(sm, stop_timer);
		enter(sm, ECALL_IDLE, "dial attempts exhausted");
		return;
	}
	sm->redial_pending = true;
	enter(sm, ECALL_DIALING, why);
	CALL(sm, start_timer, sm->cfg.redial_delay_ms);
}

static void send_msd(struct ecall_sm *sm)
{
	CALL(sm, send_msd, sm->msd_id++);
}

void ecall_sm_init(struct ecall_sm *sm, const struct ecall_cfg *cfg,
		   const struct ecall_ops *ops, void *ctx)
{
	*sm = (struct ecall_sm){ .state = ECALL_IDLE, .cfg = *cfg, .ops = ops, .ctx = ctx };
	if (!sm->cfg.max_dial_attempts)
		sm->cfg.max_dial_attempts = 1;
}

ecall_state_t ecall_sm_handle(struct ecall_sm *sm, ecall_event_t ev)
{
	switch (sm->state) {
	case ECALL_IDLE:
	case ECALL_CALLBACK_WAIT:
		if (ev == ECALL_EV_TRIGGER_AUTO) {
			start_session(sm, true);
		} else if (ev == ECALL_EV_TRIGGER_MANUAL) {
			sm->automatic = false;
			CALL(sm, stop_timer);
			enter(sm, ECALL_CANCEL_WINDOW, "manual trigger");
			CALL(sm, start_timer, sm->cfg.cancel_window_ms);
		} else if (sm->state == ECALL_CALLBACK_WAIT) {
			if (ev == ECALL_EV_INCOMING_CALL) {
				CALL(sm, stop_timer);
				CALL(sm, answer);
				CALL(sm, audio_route, true);
				enter(sm, ECALL_VOICE, "PSAP call-back");
			} else if (ev == ECALL_EV_TIMER) {
				enter(sm, ECALL_IDLE, "call-back window expired");
			}
		}
		break;

	case ECALL_CANCEL_WINDOW:
		if (ev == ECALL_EV_CANCEL) {
			CALL(sm, stop_timer);
			enter(sm, ECALL_IDLE, "cancelled by occupant");
		} else if (ev == ECALL_EV_TIMER) {
			start_session(sm, false);
		} else if (ev == ECALL_EV_TRIGGER_AUTO) {
			/* A crash during the manual window escalates; no cancel. */
			start_session(sm, true);
		}
		break;

	case ECALL_DIALING:
		if (ev == ECALL_EV_CALL_CONNECTED && !sm->redial_pending) {
			CALL(sm, stop_timer);
			enter(sm, ECALL_MSD_TX, "connected");
			send_msd(sm);
			CALL(sm, start_timer, sm->cfg.msd_ack_timeout_ms);
		} else if (ev == ECALL_EV_CALL_FAILED && !sm->redial_pending) {
			schedule_redial(sm, "call failed");
		} else if (ev == ECALL_EV_TIMER && sm->redial_pending) {
			dial(sm);
		}
		break;

	case ECALL_MSD_TX:
		if (ev == ECALL_EV_MSD_ACK) {
			sm->msd_acked = true;
			CALL(sm, stop_timer);
			CALL(sm, audio_route, true);
			enter(sm, ECALL_VOICE, "MSD acknowledged");
		} else if (ev == ECALL_EV_TIMER) {
			/* Never leave occupants without voice because of the data link. */
			CALL(sm, audio_route, true);
			enter(sm, ECALL_VOICE, "MSD ack timeout");
		} else if (ev == ECALL_EV_MSD_REQUEST) {
			send_msd(sm);
		} else if (ev == ECALL_EV_CALL_ENDED || ev == ECALL_EV_CALL_FAILED) {
			CALL(sm, stop_timer);
			schedule_redial(sm, "dropped before MSD ack");
		}
		break;

	case ECALL_VOICE:
		if (ev == ECALL_EV_MSD_REQUEST) {
			send_msd(sm);
		} else if (ev == ECALL_EV_CALL_ENDED || ev == ECALL_EV_CALL_FAILED) {
			CALL(sm, audio_route, false);
			if (!sm->msd_acked && ev == ECALL_EV_CALL_FAILED) {
				schedule_redial(sm, "dropped during voice");
				break;
			}
			enter(sm, ECALL_CALLBACK_WAIT, "cleared down");
			CALL(sm, start_timer, sm->cfg.callback_window_ms);
		}
		break;

	default:
		break;
	}
	return sm->state;
}
