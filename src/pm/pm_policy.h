/* SPDX-License-Identifier: MIT */
#ifndef TCU_PM_POLICY_H
#define TCU_PM_POLICY_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Platform power policy for a truck TCU on permanent battery (KL30).
 *
 *   ACTIVE      ignition on, or an eCall in progress (always wins)
 *   GRACE       ignition off; keep running for uploads/OTA, honour wakelocks
 *   STANDBY     suspend-to-RAM; modem in PSM/eDRX, wake on modem RI, CAN,
 *               ignition, and a periodic RTC alarm for fleet keep-alive
 *   DEEP_SLEEP  modem and peripherals off; wake on ignition only
 *
 * The policy is pure: tcu-pmd samples inputs and executes the result.
 */

typedef enum {
	PM_ACTIVE,
	PM_GRACE,
	PM_STANDBY,
	PM_DEEP_SLEEP,
	PM_STATE_COUNT
} pm_state_t;

struct pm_inputs {
	uint64_t now_ms;      /* CLOCK_BOOTTIME: keeps counting in suspend */
	bool ignition;
	bool ecall_active;
	unsigned int wakelocks;
	uint32_t battery_mv;
};

struct pm_cfg {
	uint32_t grace_ms;        /* e.g. 5 min */
	uint32_t max_grace_ms;    /* hard cap even with wakelocks, e.g. 30 min */
	uint64_t max_standby_ms;  /* e.g. 72 h before deep sleep */
	uint32_t low_batt_mv;     /* 24 V truck: e.g. 23000 */
	uint32_t crit_batt_mv;    /* e.g. 21500: skip standby entirely */
};

struct pm_policy {
	pm_state_t state;
	struct pm_cfg cfg;
	uint64_t ign_off_ms;
	uint64_t standby_since_ms;
};

void pm_policy_init(struct pm_policy *p, const struct pm_cfg *cfg, uint64_t now_ms);
pm_state_t pm_policy_eval(struct pm_policy *p, const struct pm_inputs *in);
const char *pm_state_name(pm_state_t s);

#endif
