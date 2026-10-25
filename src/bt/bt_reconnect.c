// SPDX-License-Identifier: MIT
#include "bt_reconnect.h"

#include <stddef.h>

static const char *const names[BTR_STATE_COUNT] = {
	[BTR_IDLE] = "IDLE",
	[BTR_WAIT_ADAPTER] = "WAIT_ADAPTER",
	[BTR_PAGING] = "PAGING",
	[BTR_CONNECTED] = "CONNECTED",
	[BTR_BACKOFF] = "BACKOFF",
	[BTR_NEEDS_REPAIR] = "NEEDS_REPAIR",
	[BTR_SUSPENDED] = "SUSPENDED",
};

const char *btr_state_name(btr_state_t s)
{
	return s < BTR_STATE_COUNT ? names[s] : "?";
}

#define CALL(b, fn, ...) do { if ((b)->ops->fn) (b)->ops->fn((b)->ctx, ##__VA_ARGS__); } while (0)

static void enter(struct btr *b, btr_state_t s, const char *why)
{
	b->state = s;
	CALL(b, notify, s, why);
}

static void page(struct btr *b)
{
	if (!b->adapter_on) {
		enter(b, BTR_WAIT_ADAPTER, "adapter off");
		return;
	}
	b->attempts++;
	enter(b, BTR_PAGING, "connect");
	CALL(b, connect);
}

static void retry_later(struct btr *b, uint32_t ms, const char *why)
{
	if (b->attempts >= b->cfg.max_attempts) {
		CALL(b, start_timer, 0);
		enter(b, BTR_IDLE, "retry budget spent");
		return;
	}
	enter(b, BTR_BACKOFF, why);
	CALL(b, start_timer, ms);
}

static void start_cycle(struct btr *b)
{
	b->attempts = 0;
	backoff_reset(&b->backoff);
	page(b);
}

void btr_init(struct btr *b, const struct btr_cfg *cfg,
	      const struct btr_ops *ops, void *ctx)
{
	*b = (struct btr){ .state = BTR_IDLE, .cfg = *cfg, .ops = ops, .ctx = ctx };
	if (!b->cfg.max_attempts)
		b->cfg.max_attempts = 20;
	backoff_init(&b->backoff, b->cfg.base_ms, b->cfg.max_ms, 25, 0);
}

btr_state_t btr_handle(struct btr *b, btr_event_t ev, int arg)
{
	/* Events that apply in every state. */
	switch (ev) {
	case BTR_EV_ADAPTER_OFF:
		b->adapter_on = false;
		if (b->state == BTR_PAGING || b->state == BTR_CONNECTED ||
		    b->state == BTR_BACKOFF) {
			CALL(b, start_timer, 0);
			enter(b, BTR_WAIT_ADAPTER, "adapter lost");
		}
		return b->state;
	case BTR_EV_IGNITION_OFF:
		b->ignition = false;
		if (b->state == BTR_PAGING)
			CALL(b, cancel);
		CALL(b, start_timer, 0);
		if (b->state != BTR_CONNECTED && b->state != BTR_NEEDS_REPAIR)
			enter(b, BTR_IDLE, "ignition off");
		return b->state;
	case BTR_EV_CONNECTED:
		/* Whoever connected first wins; drop our own page if any. */
		if (b->state == BTR_PAGING)
			CALL(b, cancel);
		if (b->state != BTR_CONNECTED) {
			CALL(b, start_timer, 0);
			backoff_reset(&b->backoff);
			b->attempts = 0;
			enter(b, BTR_CONNECTED, "connected");
		}
		return b->state;
	case BTR_EV_SUSPEND:
		if (b->state != BTR_SUSPENDED) {
			b->before_suspend = b->state;
			b->suspended_by_ecall = true;
			if (b->state == BTR_PAGING)
				CALL(b, cancel);
			CALL(b, start_timer, 0);
			/* An existing link is kept; we only stop paging. */
			if (b->state != BTR_CONNECTED)
				enter(b, BTR_SUSPENDED, "eCall active");
		}
		return b->state;
	default:
		break;
	}

	switch (b->state) {
	case BTR_IDLE:
		if (ev == BTR_EV_IGNITION_ON) {
			b->ignition = true;
			start_cycle(b);
		} else if (ev == BTR_EV_ADAPTER_ON) {
			b->adapter_on = true;
		}
		break;

	case BTR_WAIT_ADAPTER:
		if (ev == BTR_EV_ADAPTER_ON) {
			b->adapter_on = true;
			if (b->ignition)
				start_cycle(b);
			else
				enter(b, BTR_IDLE, "adapter on, ignition off");
		} else if (ev == BTR_EV_IGNITION_ON) {
			b->ignition = true;
		}
		break;

	case BTR_PAGING:
		if (ev == BTR_EV_CONNECT_OK) {
			btr_handle(b, BTR_EV_CONNECTED, 0);
		} else if (ev == BTR_EV_CONNECT_FAIL) {
			switch ((btr_error_t)arg) {
			case BTR_ERR_AUTH:
				enter(b, BTR_NEEDS_REPAIR, "link key rejected");
				break;
			case BTR_ERR_IN_PROGRESS:
				/* Phone is connecting to us; wait for Connected=true. */
				retry_later(b, b->cfg.base_ms, "collision");
				break;
			default:
				retry_later(b, backoff_next(&b->backoff), "connect failed");
				break;
			}
		}
		break;

	case BTR_CONNECTED:
		if (ev == BTR_EV_DISCONNECTED && b->suspended_by_ecall) {
			/* Do not page during an eCall; reconnect once it ends. */
			b->before_suspend = BTR_BACKOFF;
			enter(b, BTR_SUSPENDED, "link lost during eCall");
		} else if (ev == BTR_EV_DISCONNECTED) {
			switch ((btr_disc_reason_t)arg) {
			case BTR_DISC_REMOTE_USER:
				enter(b, BTR_SUSPENDED, "user disconnected on phone");
				break;
			case BTR_DISC_LINK_LOSS:
				if (!b->ignition) {
					enter(b, BTR_IDLE, "link loss, ignition off");
					break;
				}
				b->attempts = 0;
				backoff_reset(&b->backoff);
				retry_later(b, b->cfg.link_loss_first_ms, "link loss");
				break;
			default:
				enter(b, BTR_IDLE, "local disconnect");
				break;
			}
		} else if (ev == BTR_EV_RESUME) {
			b->suspended_by_ecall = false;
		}
		break;

	case BTR_BACKOFF:
		if (ev == BTR_EV_TIMER)
			page(b);
		break;

	case BTR_NEEDS_REPAIR:
		if (ev == BTR_EV_PAIRED)
			start_cycle(b);
		break;

	case BTR_SUSPENDED:
		if (ev == BTR_EV_RESUME && b->suspended_by_ecall) {
			b->suspended_by_ecall = false;
			if (b->ignition && b->before_suspend != BTR_IDLE &&
			    b->before_suspend != BTR_NEEDS_REPAIR)
				start_cycle(b);
			else
				enter(b, b->before_suspend, "resume");
		} else if (ev == BTR_EV_IGNITION_ON) {
			/* New drive: a user disconnect no longer applies. */
			b->ignition = true;
			b->suspended_by_ecall = false;
			start_cycle(b);
		}
		break;

	default:
		break;
	}
	return b->state;
}
