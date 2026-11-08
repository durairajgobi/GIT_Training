/* SPDX-License-Identifier: MIT */
#ifndef TCU_LTE_SM_H
#define TCU_LTE_SM_H

#include <stdbool.h>
#include <stdint.h>

#include "backoff.h"

/*
 * LTE connection manager. Brings the modem from power-on to a working
 * data bearer and keeps it there: SIM check, registration, PDN context
 * activation, and recovery with backoff (radio cycle, then modem re-init).
 */

typedef enum {
	LTE_OFF,
	LTE_MODEM_INIT,    /* sending init command list */
	LTE_SIM_WAIT,
	LTE_SEARCHING,     /* radio on, waiting for +CEREG home/roaming */
	LTE_PDN_CONNECTING,
	LTE_CONNECTED,
	LTE_BACKOFF,
	LTE_STATE_COUNT
} lte_state_t;

typedef enum {
	LTE_EV_START,
	LTE_EV_STOP,
	LTE_EV_AT_OK,
	LTE_EV_AT_ERROR,
	LTE_EV_SIM_READY,
	LTE_EV_SIM_NOT_READY,
	LTE_EV_REG_HOME,
	LTE_EV_REG_ROAMING,
	LTE_EV_REG_LOST,     /* searching / not registered */
	LTE_EV_REG_DENIED,
	LTE_EV_TIMER,
} lte_event_t;

struct lte_ops {
	void (*send)(void *ctx, const char *cmd);
	void (*start_timer)(void *ctx, uint32_t ms);
	void (*netif)(void *ctx, bool up); /* bring wwan0 / DHCP up or down */
	void (*notify)(void *ctx, lte_state_t s, const char *reason);
};

struct lte_cfg {
	char apn[64];
	bool roaming_allowed;
	uint32_t at_timeout_ms;     /* per command, e.g. 5 s */
	uint32_t search_timeout_ms; /* e.g. 180 s before radio cycle */
	uint32_t max_radio_cycles;  /* then full modem re-init */

	/* Multi-RAT (see rat.h). All off = LTE-only behaviour. */
	bool urc_ps;                /* AT+CGREG=2: 2G/3G packet domain */
	bool urc_5gs;               /* AT+C5GREG=2: 5G standalone */
	char rat_cmd[96];           /* RAT preference, sent with radio off */
};

#define LTE_MAX_INIT_CMDS 8

struct lte_sm {
	lte_state_t state;
	struct lte_cfg cfg;
	const struct lte_ops *ops;
	void *ctx;
	backoff_t backoff;
	const char *init_cmds[LTE_MAX_INIT_CMDS];
	bool init_optional[LTE_MAX_INIT_CMDS]; /* ERROR is not fatal */
	unsigned int n_init;
	unsigned int init_step;
	unsigned int pdn_step;
	unsigned int radio_cycles;
	bool reinit_after_backoff;
	char cmd[160];
};

void lte_sm_init(struct lte_sm *sm, const struct lte_cfg *cfg,
		 const struct lte_ops *ops, void *ctx);
lte_state_t lte_sm_handle(struct lte_sm *sm, lte_event_t ev);
const char *lte_state_name(lte_state_t s);

/* Change the RAT preference used on the next (re-)init. "" clears it. */
void lte_sm_set_rat_cmd(struct lte_sm *sm, const char *cmd);

#endif
