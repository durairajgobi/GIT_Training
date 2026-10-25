// SPDX-License-Identifier: MIT
#include "bt_reconnect.h"
#include "check.h"

struct rec { int connects, cancels; uint32_t timer; };

static void op_connect(void *c) { ((struct rec *)c)->connects++; }
static void op_cancel(void *c) { ((struct rec *)c)->cancels++; }
static void op_timer(void *c, uint32_t ms) { ((struct rec *)c)->timer = ms; }

static const struct btr_ops ops = { .connect = op_connect, .cancel = op_cancel, .start_timer = op_timer };
static const struct btr_cfg cfg = { .link_loss_first_ms = 1000, .base_ms = 2000, .max_ms = 30000, .max_attempts = 4 };

static void setup(struct btr *b, struct rec *r)
{
	*r = (struct rec){ 0 };
	btr_init(b, &cfg, &ops, r);
	btr_handle(b, BTR_EV_ADAPTER_ON, 0);
	CHECK_EQ(btr_handle(b, BTR_EV_IGNITION_ON, 0), BTR_PAGING);
	CHECK_EQ(r->connects, 1);
}

int main(void)
{
	struct btr b;
	struct rec r;

	/* Fix 1: phone wins the page race; late InProgress is harmless. */
	setup(&b, &r);
	CHECK_EQ(btr_handle(&b, BTR_EV_CONNECTED, 0), BTR_CONNECTED);
	CHECK_EQ(r.cancels, 1);
	CHECK_EQ(btr_handle(&b, BTR_EV_CONNECT_FAIL, BTR_ERR_IN_PROGRESS), BTR_CONNECTED);

	/* Fix 2: stale link key stops the loop. */
	setup(&b, &r);
	CHECK_EQ(btr_handle(&b, BTR_EV_CONNECT_FAIL, BTR_ERR_AUTH), BTR_NEEDS_REPAIR);
	CHECK_EQ(btr_handle(&b, BTR_EV_TIMER, 0), BTR_NEEDS_REPAIR);
	CHECK_EQ(r.connects, 1);
	CHECK_EQ(btr_handle(&b, BTR_EV_PAIRED, 0), BTR_PAGING);

	/* Fix 3: user disconnect on phone is respected until next drive. */
	setup(&b, &r);
	btr_handle(&b, BTR_EV_CONNECT_OK, 0);
	CHECK_EQ(btr_handle(&b, BTR_EV_DISCONNECTED, BTR_DISC_REMOTE_USER), BTR_SUSPENDED);
	CHECK_EQ(btr_handle(&b, BTR_EV_TIMER, 0), BTR_SUSPENDED);
	btr_handle(&b, BTR_EV_IGNITION_OFF, 0);
	CHECK_EQ(btr_handle(&b, BTR_EV_IGNITION_ON, 0), BTR_PAGING);

	/* Link loss: fast first retry, then backoff, then give up. */
	setup(&b, &r);
	btr_handle(&b, BTR_EV_CONNECT_OK, 0);
	CHECK_EQ(btr_handle(&b, BTR_EV_DISCONNECTED, BTR_DISC_LINK_LOSS), BTR_BACKOFF);
	CHECK_EQ(r.timer, 1000);
	for (int i = 0; i < 4; i++) {
		CHECK_EQ(btr_handle(&b, BTR_EV_TIMER, 0), BTR_PAGING);
		btr_handle(&b, BTR_EV_CONNECT_FAIL, BTR_ERR_PAGE_TIMEOUT);
	}
	CHECK_EQ(b.state, BTR_IDLE);
	CHECK_EQ(r.connects, 5);

	/* Fix 4: adapter reset mid-page resumes once the adapter is back. */
	setup(&b, &r);
	CHECK_EQ(btr_handle(&b, BTR_EV_ADAPTER_OFF, 0), BTR_WAIT_ADAPTER);
	CHECK_EQ(btr_handle(&b, BTR_EV_ADAPTER_ON, 0), BTR_PAGING);

	/* eCall: stop paging, resume afterwards. */
	setup(&b, &r);
	btr_handle(&b, BTR_EV_CONNECT_FAIL, BTR_ERR_PAGE_TIMEOUT);
	CHECK_EQ(btr_handle(&b, BTR_EV_SUSPEND, 0), BTR_SUSPENDED);
	CHECK_EQ(btr_handle(&b, BTR_EV_TIMER, 0), BTR_SUSPENDED);
	CHECK_EQ(btr_handle(&b, BTR_EV_RESUME, 0), BTR_PAGING);

	/* eCall keeps an existing link; link loss during it does not page. */
	setup(&b, &r);
	btr_handle(&b, BTR_EV_CONNECT_OK, 0);
	CHECK_EQ(btr_handle(&b, BTR_EV_SUSPEND, 0), BTR_CONNECTED);
	CHECK_EQ(btr_handle(&b, BTR_EV_DISCONNECTED, BTR_DISC_LINK_LOSS), BTR_SUSPENDED);
	CHECK_EQ(btr_handle(&b, BTR_EV_RESUME, 0), BTR_PAGING);

	/* Ignition off with no link: idle, no paging. */
	setup(&b, &r);
	CHECK_EQ(btr_handle(&b, BTR_EV_IGNITION_OFF, 0), BTR_IDLE);
	CHECK_DONE();
}
