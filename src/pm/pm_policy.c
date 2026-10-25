// SPDX-License-Identifier: MIT
#include "pm_policy.h"

static const char *const names[PM_STATE_COUNT] = {
	[PM_ACTIVE] = "ACTIVE",
	[PM_GRACE] = "GRACE",
	[PM_STANDBY] = "STANDBY",
	[PM_DEEP_SLEEP] = "DEEP_SLEEP",
};

const char *pm_state_name(pm_state_t s)
{
	return s < PM_STATE_COUNT ? names[s] : "?";
}

void pm_policy_init(struct pm_policy *p, const struct pm_cfg *cfg, uint64_t now_ms)
{
	*p = (struct pm_policy){ .state = PM_ACTIVE, .cfg = *cfg, .ign_off_ms = now_ms };
}

pm_state_t pm_policy_eval(struct pm_policy *p, const struct pm_inputs *in)
{
	pm_state_t next = p->state;

	/* Regulatory: an eCall keeps the unit fully up regardless of battery. */
	if (in->ecall_active || in->ignition) {
		next = PM_ACTIVE;
		goto out;
	}

	switch (p->state) {
	case PM_ACTIVE:
		p->ign_off_ms = in->now_ms;
		next = PM_GRACE;
		/* a critically low battery skips the grace period */
		__attribute__((fallthrough));
	case PM_GRACE: {
		uint64_t off = in->now_ms - p->ign_off_ms;

		if (in->battery_mv && in->battery_mv < p->cfg.crit_batt_mv)
			next = PM_DEEP_SLEEP;
		else if (off >= p->cfg.max_grace_ms ||
			 (off >= p->cfg.grace_ms && in->wakelocks == 0))
			next = PM_STANDBY;
		break;
	}
	case PM_STANDBY:
		if ((in->battery_mv && in->battery_mv < p->cfg.low_batt_mv) ||
		    in->now_ms - p->standby_since_ms >= p->cfg.max_standby_ms)
			next = PM_DEEP_SLEEP;
		break;
	case PM_DEEP_SLEEP:
	default:
		break;
	}

out:
	if (next == PM_STANDBY && p->state != PM_STANDBY)
		p->standby_since_ms = in->now_ms;
	p->state = next;
	return next;
}
