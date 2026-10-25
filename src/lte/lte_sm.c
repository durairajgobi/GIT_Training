// SPDX-License-Identifier: MIT
#include "lte_sm.h"

#include <stdio.h>
#include <string.h>

#include "tcu_log.h"

static const char *const names[LTE_STATE_COUNT] = {
	[LTE_OFF] = "OFF",
	[LTE_MODEM_INIT] = "MODEM_INIT",
	[LTE_SIM_WAIT] = "SIM_WAIT",
	[LTE_SEARCHING] = "SEARCHING",
	[LTE_PDN_CONNECTING] = "PDN_CONNECTING",
	[LTE_CONNECTED] = "CONNECTED",
	[LTE_BACKOFF] = "BACKOFF",
};

/* Echo off, verbose errors, registration URCs with cell info, radio off. */
static const char *const init_cmds[] = {
	"ATE0", "AT+CMEE=1", "AT+CEREG=2", "AT+CFUN=4",
};

const char *lte_state_name(lte_state_t s)
{
	return s < LTE_STATE_COUNT ? names[s] : "?";
}

#define CALL(sm, fn, ...) do { if ((sm)->ops->fn) (sm)->ops->fn((sm)->ctx, ##__VA_ARGS__); } while (0)

static void enter(struct lte_sm *sm, lte_state_t s, const char *why)
{
	sm->state = s;
	CALL(sm, notify, s, why);
}

static void send(struct lte_sm *sm, const char *cmd, uint32_t timeout)
{
	CALL(sm, send, cmd);
	CALL(sm, start_timer, timeout);
}

static void start_init(struct lte_sm *sm)
{
	sm->init_step = 0;
	sm->radio_cycles = 0;
	enter(sm, LTE_MODEM_INIT, "init");
	send(sm, init_cmds[0], sm->cfg.at_timeout_ms);
}

static void start_search(struct lte_sm *sm, const char *why)
{
	enter(sm, LTE_SEARCHING, why);
	CALL(sm, send, "AT+CFUN=1");
	CALL(sm, start_timer, sm->cfg.search_timeout_ms);
}

/* PDN bring-up: define context, then activate it. */
static void pdn_step(struct lte_sm *sm)
{
	if (sm->pdn_step == 0)
		snprintf(sm->cmd, sizeof(sm->cmd), "AT+CGDCONT=1,\"IPV4V6\",\"%s\"", sm->cfg.apn);
	else
		snprintf(sm->cmd, sizeof(sm->cmd), "AT+CGACT=1,1");
	send(sm, sm->cmd, sm->cfg.at_timeout_ms * 6);
}

static void start_pdn(struct lte_sm *sm)
{
	sm->pdn_step = 0;
	enter(sm, LTE_PDN_CONNECTING, "registered");
	pdn_step(sm);
}

static void go_backoff(struct lte_sm *sm, bool reinit, const char *why)
{
	if (sm->state == LTE_CONNECTED)
		CALL(sm, netif, false);
	sm->reinit_after_backoff = reinit;
	enter(sm, LTE_BACKOFF, why);
	CALL(sm, start_timer, backoff_next(&sm->backoff));
}

static bool registered(struct lte_sm *sm, lte_event_t ev)
{
	return ev == LTE_EV_REG_HOME || (ev == LTE_EV_REG_ROAMING && sm->cfg.roaming_allowed);
}

void lte_sm_init(struct lte_sm *sm, const struct lte_cfg *cfg,
		 const struct lte_ops *ops, void *ctx)
{
	memset(sm, 0, sizeof(*sm));
	sm->state = LTE_OFF;
	sm->cfg = *cfg;
	sm->ops = ops;
	sm->ctx = ctx;
	if (!sm->cfg.max_radio_cycles)
		sm->cfg.max_radio_cycles = 3;
	backoff_init(&sm->backoff, 2000, 300000, 20, 0);
}

lte_state_t lte_sm_handle(struct lte_sm *sm, lte_event_t ev)
{
	if (ev == LTE_EV_STOP) {
		if (sm->state == LTE_CONNECTED)
			CALL(sm, netif, false);
		CALL(sm, send, "AT+CFUN=4");
		CALL(sm, start_timer, 0);
		enter(sm, LTE_OFF, "stop");
		return sm->state;
	}

	switch (sm->state) {
	case LTE_OFF:
		if (ev == LTE_EV_START) {
			backoff_reset(&sm->backoff);
			start_init(sm);
		}
		break;

	case LTE_MODEM_INIT:
		if (ev == LTE_EV_AT_OK) {
			if (++sm->init_step < ARRAY_SIZE(init_cmds)) {
				send(sm, init_cmds[sm->init_step], sm->cfg.at_timeout_ms);
			} else {
				enter(sm, LTE_SIM_WAIT, "init done");
				send(sm, "AT+CPIN?", sm->cfg.at_timeout_ms);
			}
		} else if (ev == LTE_EV_AT_ERROR || ev == LTE_EV_TIMER) {
			go_backoff(sm, true, "init failed");
		}
		break;

	case LTE_SIM_WAIT:
		if (ev == LTE_EV_SIM_READY)
			start_search(sm, "SIM ready");
		else if (ev == LTE_EV_SIM_NOT_READY || ev == LTE_EV_AT_ERROR ||
			 ev == LTE_EV_TIMER)
			go_backoff(sm, true, "SIM not ready");
		break;

	case LTE_SEARCHING:
		if (registered(sm, ev)) {
			start_pdn(sm);
		} else if (ev == LTE_EV_REG_DENIED) {
			/* Network rejected us: retrying fast only earns a longer ban. */
			go_backoff(sm, false, "registration denied");
		} else if (ev == LTE_EV_TIMER) {
			if (++sm->radio_cycles >= sm->cfg.max_radio_cycles)
				go_backoff(sm, true, "search timeout, re-init modem");
			else
				go_backoff(sm, false, "search timeout, cycle radio");
		}
		break;

	case LTE_PDN_CONNECTING:
		if (ev == LTE_EV_AT_OK) {
			if (++sm->pdn_step < 2) {
				pdn_step(sm);
			} else {
				CALL(sm, start_timer, 0);
				backoff_reset(&sm->backoff);
				sm->radio_cycles = 0;
				enter(sm, LTE_CONNECTED, "PDN active");
				CALL(sm, netif, true);
			}
		} else if (ev == LTE_EV_AT_ERROR || ev == LTE_EV_TIMER) {
			go_backoff(sm, false, "PDN activation failed");
		} else if (ev == LTE_EV_REG_LOST || ev == LTE_EV_REG_DENIED) {
			start_search(sm, "lost registration");
		}
		break;

	case LTE_CONNECTED:
		if (ev == LTE_EV_REG_LOST || ev == LTE_EV_REG_DENIED ||
		    (ev == LTE_EV_REG_ROAMING && !sm->cfg.roaming_allowed)) {
			CALL(sm, netif, false);
			start_search(sm, "lost registration");
		}
		break;

	case LTE_BACKOFF:
		if (ev == LTE_EV_TIMER) {
			if (sm->reinit_after_backoff) {
				start_init(sm);
			} else {
				CALL(sm, send, "AT+CFUN=4");
				start_search(sm, "retry");
			}
		}
		break;

	default:
		break;
	}
	return sm->state;
}
