// SPDX-License-Identifier: MIT
#include "check.h"
#include "lte_sm.h"

struct rec {
	char last[160];
	int sends, netif_up, netif_down;
	uint32_t timer;
};

static void op_send(void *c, const char *cmd)
{
	struct rec *r = c;

	snprintf(r->last, sizeof(r->last), "%s", cmd);
	r->sends++;
}
static void op_timer(void *c, uint32_t ms) { ((struct rec *)c)->timer = ms; }
static void op_netif(void *c, bool up) { up ? ((struct rec *)c)->netif_up++ : ((struct rec *)c)->netif_down++; }

static const struct lte_ops ops = { .send = op_send, .start_timer = op_timer, .netif = op_netif };

static void bring_up(struct lte_sm *sm, struct rec *r)
{
	CHECK_EQ(lte_sm_handle(sm, LTE_EV_START), LTE_MODEM_INIT);
	CHECK_STR(r->last, "ATE0");
	for (int i = 0; i < 4; i++)
		lte_sm_handle(sm, LTE_EV_AT_OK);
	CHECK_EQ(sm->state, LTE_SIM_WAIT);
	CHECK_STR(r->last, "AT+CPIN?");
	CHECK_EQ(lte_sm_handle(sm, LTE_EV_SIM_READY), LTE_SEARCHING);
	CHECK_STR(r->last, "AT+CFUN=1");
	CHECK_EQ(lte_sm_handle(sm, LTE_EV_REG_HOME), LTE_PDN_CONNECTING);
	CHECK_STR(r->last, "AT+CGDCONT=1,\"IPV4V6\",\"fleet.apn\"");
	lte_sm_handle(sm, LTE_EV_AT_OK);
	CHECK_STR(r->last, "AT+CGACT=1,1");
	CHECK_EQ(lte_sm_handle(sm, LTE_EV_AT_OK), LTE_CONNECTED);
}

int main(void)
{
	struct lte_cfg cfg = {
		.apn = "fleet.apn", .at_timeout_ms = 5000, .search_timeout_ms = 180000,
		.max_radio_cycles = 2,
	};
	struct rec r = { 0 };
	struct lte_sm sm;

	lte_sm_init(&sm, &cfg, &ops, &r);
	bring_up(&sm, &r);
	CHECK_EQ(r.netif_up, 1);

	/* Roaming not allowed: drop the bearer and search again. */
	CHECK_EQ(lte_sm_handle(&sm, LTE_EV_REG_ROAMING), LTE_SEARCHING);
	CHECK_EQ(r.netif_down, 1);
	CHECK_EQ(lte_sm_handle(&sm, LTE_EV_REG_ROAMING), LTE_SEARCHING);

	/* Search timeouts: cycle radio, then full re-init. */
	CHECK_EQ(lte_sm_handle(&sm, LTE_EV_TIMER), LTE_BACKOFF);
	CHECK(!sm.reinit_after_backoff);
	CHECK_EQ(lte_sm_handle(&sm, LTE_EV_TIMER), LTE_SEARCHING);
	CHECK_EQ(lte_sm_handle(&sm, LTE_EV_TIMER), LTE_BACKOFF);
	CHECK(sm.reinit_after_backoff);
	CHECK_EQ(lte_sm_handle(&sm, LTE_EV_TIMER), LTE_MODEM_INIT);

	/* Registration denied backs off without re-init. */
	for (int i = 0; i < 4; i++)
		lte_sm_handle(&sm, LTE_EV_AT_OK);
	lte_sm_handle(&sm, LTE_EV_SIM_READY);
	CHECK_EQ(lte_sm_handle(&sm, LTE_EV_REG_DENIED), LTE_BACKOFF);
	CHECK(!sm.reinit_after_backoff);

	/* SIM missing -> backoff + re-init. */
	lte_sm_handle(&sm, LTE_EV_STOP);
	lte_sm_handle(&sm, LTE_EV_START);
	for (int i = 0; i < 4; i++)
		lte_sm_handle(&sm, LTE_EV_AT_OK);
	CHECK_EQ(lte_sm_handle(&sm, LTE_EV_SIM_NOT_READY), LTE_BACKOFF);
	CHECK(sm.reinit_after_backoff);

	/* Roaming allowed config connects on roaming registration. */
	cfg.roaming_allowed = true;
	lte_sm_init(&sm, &cfg, &ops, &r);
	lte_sm_handle(&sm, LTE_EV_START);
	for (int i = 0; i < 4; i++)
		lte_sm_handle(&sm, LTE_EV_AT_OK);
	lte_sm_handle(&sm, LTE_EV_SIM_READY);
	CHECK_EQ(lte_sm_handle(&sm, LTE_EV_REG_ROAMING), LTE_PDN_CONNECTING);
	CHECK_EQ(lte_sm_handle(&sm, LTE_EV_STOP), LTE_OFF);
	CHECK_DONE();
}
