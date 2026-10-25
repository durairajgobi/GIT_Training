// SPDX-License-Identifier: MIT
/*
 * tcu-pmd - platform power manager.
 *
 *   tcu-pmd -g /sys/class/gpio/gpio42/value -b /sys/bus/iio/devices/iio:device0
 *           [-k 11.0] [-H /etc/tcu-pm/hook]
 *
 * Samples ignition, battery and wakelocks at 1 Hz, runs pm_policy, and
 * performs the transition. Wake sources (modem RI, CAN transceiver INH,
 * ignition, RTC) are enabled in the device tree and via
 * /sys/devices/.../power/wakeup by the boot-time hook.
 *
 * Applications that need the system awake take a wakelock through
 * /sys/power/wake_lock (CONFIG_PM_WAKELOCKS); tcu-pmd only counts them.
 */
#include <errno.h>
#include <getopt.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "pm_policy.h"
#include "tcu_log.h"

extern char **environ;

#define ECALL_FLAG "/run/ecalld.active"
#define IGNITION_EXPORT "/run/tcu/ignition"
#define KEEPALIVE_S 3600     /* RTC wake for fleet heartbeat */
#define RESUME_HOLD_MS 15000 /* time for apps to grab a wakelock after wake */

static const char *hook = "/etc/tcu-pm/hook";

static uint64_t boottime_ms(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_BOOTTIME, &ts);
	return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

static int read_long(const char *path, long *v)
{
	FILE *f = fopen(path, "r");
	int ok;

	if (!f)
		return -1;
	ok = fscanf(f, "%ld", v) == 1;
	fclose(f);
	return ok ? 0 : -1;
}

static int read_double(const char *path, double *v)
{
	FILE *f = fopen(path, "r");
	int ok;

	if (!f)
		return -1;
	ok = fscanf(f, "%lf", v) == 1;
	fclose(f);
	return ok ? 0 : -1;
}

static int write_str(const char *path, const char *s)
{
	FILE *f = fopen(path, "w");
	int rc;

	if (!f)
		return -1;
	rc = fputs(s, f) < 0 ? -1 : 0;
	if (fclose(f) != 0)
		rc = -1;
	return rc;
}

/* Battery through the ADC: raw * scale (mV at pin) * divider ratio. */
static uint32_t read_battery_mv(const char *iio, double divider)
{
	char p[256];
	long raw;
	double scale;

	if (!iio)
		return 0;
	snprintf(p, sizeof(p), "%s/in_voltage0_raw", iio);
	if (read_long(p, &raw) < 0)
		return 0;
	snprintf(p, sizeof(p), "%s/in_voltage_scale", iio);
	if (read_double(p, &scale) < 0)
		return 0;
	return (uint32_t)(raw * scale * divider);
}

static unsigned int count_wakelocks(void)
{
	char buf[4096];
	FILE *f = fopen("/sys/power/wake_lock", "r");
	unsigned int n = 0;
	size_t len;

	if (!f)
		return 0;
	len = fread(buf, 1, sizeof(buf) - 1, f);
	fclose(f);
	buf[len] = '\0';
	for (char *tok = strtok(buf, " \n"); tok; tok = strtok(NULL, " \n"))
		n++;
	return n;
}

static void run_hook(const char *arg)
{
	char *argv[] = { (char *)hook, (char *)arg, NULL };
	pid_t pid;

	if (access(hook, X_OK) != 0)
		return;
	if (posix_spawn(&pid, hook, NULL, NULL, argv, environ) == 0)
		waitpid(pid, NULL, 0);
}

static void enter_standby(void)
{
	char alarm[32];

	run_hook("pre-standby"); /* modem PSM, CAN transceiver to standby, LEDs off */
	write_str("/sys/class/rtc/rtc0/wakealarm", "0");
	snprintf(alarm, sizeof(alarm), "+%d", KEEPALIVE_S);
	write_str("/sys/class/rtc/rtc0/wakealarm", alarm);

	LOG_I("suspend-to-RAM");
	/* Blocks until resume; fails with EBUSY if a wakeup raced us. */
	if (write_str("/sys/power/state", "mem") < 0)
		LOG_W("suspend aborted: %s", strerror(errno));
	run_hook("post-resume");
}

static void enter_deep_sleep(void)
{
	char *argv[] = { "systemctl", "poweroff", NULL };
	pid_t pid;

	LOG_I("deep sleep: power off, PMIC wakes on ignition");
	run_hook("deep-sleep"); /* modem AT+CPOF, flush logs */
	if (posix_spawnp(&pid, "systemctl", NULL, NULL, argv, environ) == 0)
		waitpid(pid, NULL, 0);
}

int main(int argc, char **argv)
{
	struct pm_cfg cfg = {
		.grace_ms = 5 * 60 * 1000, .max_grace_ms = 30 * 60 * 1000,
		.max_standby_ms = 72ULL * 3600 * 1000,
		.low_batt_mv = 23000, .crit_batt_mv = 21500,
	};
	const char *gpio = NULL, *iio = NULL;
	double divider = 11.0;
	uint64_t hold_until = 0;
	struct pm_policy pol;
	pm_state_t prev;
	int opt;

	while ((opt = getopt(argc, argv, "g:b:k:H:")) != -1) {
		switch (opt) {
		case 'g': gpio = optarg; break;
		case 'b': iio = optarg; break;
		case 'k': divider = atof(optarg); break;
		case 'H': hook = optarg; break;
		default:
			fprintf(stderr, "usage: %s -g ignition_value [-b iio_dir] [-k divider] [-H hook]\n",
				argv[0]);
			return 2;
		}
	}
	if (!gpio) {
		fprintf(stderr, "ignition input (-g) is required\n");
		return 2;
	}
	openlog("tcu-pmd", LOG_PID, LOG_DAEMON);
	pm_policy_init(&pol, &cfg, boottime_ms());
	prev = pol.state;

	for (;;) {
		struct pm_inputs in = { .now_ms = boottime_ms() };
		long ign = 0;
		pm_state_t s;

		read_long(gpio, &ign);
		in.ignition = ign != 0;
		in.ecall_active = access(ECALL_FLAG, F_OK) == 0;
		in.wakelocks = count_wakelocks();
		in.battery_mv = read_battery_mv(iio, divider);
		write_str(IGNITION_EXPORT, in.ignition ? "1\n" : "0\n");

		s = pm_policy_eval(&pol, &in);
		if (s != prev)
			LOG_I("%s -> %s (ign=%d ecall=%d wl=%u batt=%umV)", pm_state_name(prev),
			      pm_state_name(s), in.ignition, in.ecall_active, in.wakelocks,
			      in.battery_mv);

		if (s == PM_STANDBY && in.wakelocks == 0 && in.now_ms >= hold_until) {
			enter_standby();
			hold_until = boottime_ms() + RESUME_HOLD_MS;
		} else if (s == PM_DEEP_SLEEP) {
			enter_deep_sleep();
		}
		prev = s;
		sleep(1);
	}
}
