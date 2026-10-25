// SPDX-License-Identifier: MIT
#include "check.h"
#include "ecall_sm.h"

struct rec {
	int dials, msds, answers, timer_ms, audio;
	bool last_auto;
};

static void dial(void *c, bool a, bool t) { ((struct rec *)c)->dials++; ((struct rec *)c)->last_auto = a; }
static void msd(void *c, uint8_t id) { ((struct rec *)c)->msds++; }
static void answer(void *c) { ((struct rec *)c)->answers++; }
static void timer(void *c, uint32_t ms) { ((struct rec *)c)->timer_ms = (int)ms; }
static void stop(void *c) { ((struct rec *)c)->timer_ms = 0; }
static void audio(void *c, bool on) { ((struct rec *)c)->audio = on; }

static const struct ecall_ops ops = {
	.dial = dial, .send_msd = msd, .answer = answer,
	.start_timer = timer, .stop_timer = stop, .audio_route = audio,
};

static const struct ecall_cfg cfg = {
	.cancel_window_ms = 3000, .redial_delay_ms = 5000, .max_dial_attempts = 3,
	.msd_ack_timeout_ms = 20000, .callback_window_ms = 3600000,
};

static void manual_cancel(void)
{
	struct rec r = { 0 };
	struct ecall_sm sm;

	ecall_sm_init(&sm, &cfg, &ops, &r);
	CHECK_EQ(ecall_sm_handle(&sm, ECALL_EV_TRIGGER_MANUAL), ECALL_CANCEL_WINDOW);
	CHECK_EQ(r.timer_ms, 3000);
	CHECK_EQ(ecall_sm_handle(&sm, ECALL_EV_CANCEL), ECALL_IDLE);
	CHECK_EQ(r.dials, 0);
}

static void auto_happy_path(void)
{
	struct rec r = { 0 };
	struct ecall_sm sm;

	ecall_sm_init(&sm, &cfg, &ops, &r);
	CHECK_EQ(ecall_sm_handle(&sm, ECALL_EV_TRIGGER_AUTO), ECALL_DIALING);
	CHECK(r.last_auto);
	CHECK_EQ(ecall_sm_handle(&sm, ECALL_EV_CANCEL), ECALL_DIALING); /* not cancellable */
	CHECK_EQ(ecall_sm_handle(&sm, ECALL_EV_CALL_CONNECTED), ECALL_MSD_TX);
	CHECK_EQ(r.msds, 1);
	CHECK_EQ(ecall_sm_handle(&sm, ECALL_EV_MSD_REQUEST), ECALL_MSD_TX);
	CHECK_EQ(r.msds, 2);
	CHECK_EQ(sm.msd_id, 2);
	CHECK_EQ(ecall_sm_handle(&sm, ECALL_EV_MSD_ACK), ECALL_VOICE);
	CHECK_EQ(r.audio, 1);
	CHECK_EQ(ecall_sm_handle(&sm, ECALL_EV_CALL_ENDED), ECALL_CALLBACK_WAIT);
	CHECK_EQ(r.timer_ms, 3600000);
	CHECK_EQ(ecall_sm_handle(&sm, ECALL_EV_INCOMING_CALL), ECALL_VOICE);
	CHECK_EQ(r.answers, 1);
	CHECK_EQ(ecall_sm_handle(&sm, ECALL_EV_CALL_ENDED), ECALL_CALLBACK_WAIT);
	CHECK_EQ(ecall_sm_handle(&sm, ECALL_EV_TIMER), ECALL_IDLE);
}

static void redial_and_give_up(void)
{
	struct rec r = { 0 };
	struct ecall_sm sm;

	ecall_sm_init(&sm, &cfg, &ops, &r);
	ecall_sm_handle(&sm, ECALL_EV_TRIGGER_MANUAL);
	CHECK_EQ(ecall_sm_handle(&sm, ECALL_EV_TIMER), ECALL_DIALING);
	CHECK_EQ(r.dials, 1);
	CHECK_EQ(ecall_sm_handle(&sm, ECALL_EV_CALL_FAILED), ECALL_DIALING);
	CHECK_EQ(r.timer_ms, 5000);
	/* Stale connect while waiting to redial is ignored. */
	CHECK_EQ(ecall_sm_handle(&sm, ECALL_EV_CALL_CONNECTED), ECALL_DIALING);
	ecall_sm_handle(&sm, ECALL_EV_TIMER);
	CHECK_EQ(r.dials, 2);
	/* Drop before MSD ack -> redial. */
	ecall_sm_handle(&sm, ECALL_EV_CALL_CONNECTED);
	CHECK_EQ(ecall_sm_handle(&sm, ECALL_EV_CALL_ENDED), ECALL_DIALING);
	ecall_sm_handle(&sm, ECALL_EV_TIMER);
	CHECK_EQ(r.dials, 3);
	CHECK_EQ(ecall_sm_handle(&sm, ECALL_EV_CALL_FAILED), ECALL_IDLE);
}

static void msd_timeout_opens_voice(void)
{
	struct rec r = { 0 };
	struct ecall_sm sm;

	ecall_sm_init(&sm, &cfg, &ops, &r);
	ecall_sm_handle(&sm, ECALL_EV_TRIGGER_AUTO);
	ecall_sm_handle(&sm, ECALL_EV_CALL_CONNECTED);
	CHECK_EQ(ecall_sm_handle(&sm, ECALL_EV_TIMER), ECALL_VOICE);
	CHECK_EQ(r.audio, 1);
}

static void crash_during_manual_window(void)
{
	struct rec r = { 0 };
	struct ecall_sm sm;

	ecall_sm_init(&sm, &cfg, &ops, &r);
	ecall_sm_handle(&sm, ECALL_EV_TRIGGER_MANUAL);
	CHECK_EQ(ecall_sm_handle(&sm, ECALL_EV_TRIGGER_AUTO), ECALL_DIALING);
	CHECK(r.last_auto);
}

int main(void)
{
	manual_cancel();
	auto_happy_path();
	redial_and_give_up();
	msd_timeout_opens_voice();
	crash_during_manual_window();
	CHECK_DONE();
}
