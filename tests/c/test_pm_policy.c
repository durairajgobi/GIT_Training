// SPDX-License-Identifier: MIT
#include "check.h"
#include "pm_policy.h"

#define MIN (60ULL * 1000)
#define HOUR (60 * MIN)

static const struct pm_cfg cfg = {
	.grace_ms = 5 * MIN, .max_grace_ms = 30 * MIN, .max_standby_ms = 72 * HOUR,
	.low_batt_mv = 23000, .crit_batt_mv = 21500,
};

int main(void)
{
	struct pm_policy p;
	struct pm_inputs in = { .now_ms = 0, .ignition = true, .battery_mv = 25000 };

	pm_policy_init(&p, &cfg, 0);
	CHECK_EQ(pm_policy_eval(&p, &in), PM_ACTIVE);

	/* Ignition off -> grace; wakelock holds it past grace_ms ... */
	in.ignition = false;
	in.now_ms = 10 * MIN;
	in.wakelocks = 1;
	CHECK_EQ(pm_policy_eval(&p, &in), PM_GRACE);
	in.now_ms += 6 * MIN;
	CHECK_EQ(pm_policy_eval(&p, &in), PM_GRACE);
	/* ... but not past the hard cap. */
	in.now_ms = 10 * MIN + 30 * MIN;
	CHECK_EQ(pm_policy_eval(&p, &in), PM_STANDBY);

	/* eCall wakes everything, even on a low battery. */
	in.ecall_active = true;
	in.battery_mv = 20000;
	CHECK_EQ(pm_policy_eval(&p, &in), PM_ACTIVE);
	in.ecall_active = false;
	in.battery_mv = 25000;

	/* Standby long enough -> deep sleep. */
	in.wakelocks = 0;
	in.now_ms += 5 * MIN;
	CHECK_EQ(pm_policy_eval(&p, &in), PM_GRACE);
	in.now_ms += 5 * MIN;
	CHECK_EQ(pm_policy_eval(&p, &in), PM_STANDBY);
	in.now_ms += 71 * HOUR;
	CHECK_EQ(pm_policy_eval(&p, &in), PM_STANDBY);
	in.now_ms += 1 * HOUR;
	CHECK_EQ(pm_policy_eval(&p, &in), PM_DEEP_SLEEP);
	in.ignition = true;
	CHECK_EQ(pm_policy_eval(&p, &in), PM_ACTIVE);

	/* Low battery in standby -> deep sleep. */
	in.ignition = false;
	pm_policy_eval(&p, &in);
	in.now_ms += 5 * MIN;
	CHECK_EQ(pm_policy_eval(&p, &in), PM_STANDBY);
	in.battery_mv = 22800;
	CHECK_EQ(pm_policy_eval(&p, &in), PM_DEEP_SLEEP);

	/* Critical battery skips standby entirely. */
	pm_policy_init(&p, &cfg, in.now_ms);
	in.battery_mv = 21000;
	CHECK_EQ(pm_policy_eval(&p, &in), PM_DEEP_SLEEP);

	/* Unknown battery (0 mV, ADC missing) never forces deep sleep. */
	pm_policy_init(&p, &cfg, in.now_ms);
	in.battery_mv = 0;
	CHECK_EQ(pm_policy_eval(&p, &in), PM_GRACE);
	CHECK_DONE();
}
