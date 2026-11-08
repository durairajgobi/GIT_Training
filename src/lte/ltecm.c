// SPDX-License-Identifier: MIT
/*
 * ltecm - cellular (2G/3G/4G/5G) connection manager daemon.
 *
 *   ltecm -d /dev/ttyUSB2 -a <apn> [-r] [-H /etc/ltecm/netif-hook]
 *         [-R 3g,4g,5g] [-V 3gpp|quectel] [-P poll_s] [-S status_file]
 *
 * Owns the modem's AT control port, drives lte_sm, and calls a hook
 * script ("up"/"down") to configure wwan0 (DHCP, routes, DNS).
 *
 * Registration on any allowed RAT (+CGREG 2G/3G, +CEREG 4G/5G-NSA,
 * +C5GREG 5G-SA) keeps the bearer up; rat_policy picks the RAT preference
 * from signal quality and data failures, and the modem selects the cell.
 */
#include <errno.h>
#include <getopt.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/signalfd.h>
#include <sys/timerfd.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "at.h"
#include "at_chan.h"
#include "lte_sm.h"
#include "rat.h"
#include "tcu_log.h"
#include "tty.h"

extern char **environ;

struct ltecm {
	int tty;
	int tfd;          /* lte_sm timer */
	int pfd;          /* status poll / RAT policy tick */
	const char *hook;
	const char *status_path;
	struct lte_sm sm;
	struct at_reader rd;
	struct at_chan chan;
	struct rat_policy rat;
	rat_vendor_t vendor;
	lte_state_t prev_state;
	rat_t logged_rat;
	bool lost_pending;  /* combined registration lost, not yet reported */
};

static uint64_t now_ms(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

static void write_cmd(struct ltecm *d, const char *cmd, at_owner_t who)
{
	LOG_D("> %s", cmd);
	if (tty_write_cmd(d->tty, cmd) < 0) {
		LOG_E("write failed: %s", strerror(errno));
		return;
	}
	if (!at_chan_push(&d->chan, who))
		LOG_W("AT queue full, responses may be misrouted");
}

static void op_send(void *ctx, const char *cmd)
{
	write_cmd(ctx, cmd, AT_OWNER_SM);
}

static void write_status(struct ltecm *d)
{
	char tmp[256], pref[24];
	FILE *f;
	rat_t r = d->rat.serving;

	if (!d->status_path)
		return;
	snprintf(tmp, sizeof(tmp), "%s.tmp", d->status_path);
	f = fopen(tmp, "w");
	if (!f)
		return;
	fprintf(f, "state=%s\nrat=%s\nsignal_dbm=%d\npref=%s\n",
		lte_state_name(d->sm.state), rat_name(r),
		r != RAT_NONE ? d->rat.dbm[r] : 0,
		rat_mask_str(rat_policy_pref(&d->rat), pref, sizeof(pref)));
	fclose(f);
	rename(tmp, d->status_path);
}

/* Push a changed RAT preference to the modem (and into the init list). */
static void apply_rat_pref(struct ltecm *d)
{
	char cmd[96], m[24];
	rat_mask_t pref;

	if (!rat_policy_take_pref(&d->rat, &pref))
		return;
	if (rat_pref_cmd(d->vendor, pref, cmd, sizeof(cmd)) < 0)
		return;
	LOG_I("RAT preference %s", rat_mask_str(pref, m, sizeof(m)));
	lte_sm_set_rat_cmd(&d->sm, cmd);
	if (d->sm.state != LTE_OFF && d->sm.state != LTE_MODEM_INIT)
		write_cmd(d, cmd, AT_OWNER_AUX);
	write_status(d);
}

static void rat_changed(struct ltecm *d)
{
	rat_t r = d->rat.serving;

	if (r != d->logged_rat) {
		LOG_I("serving RAT %s -> %s", rat_name(d->logged_rat), rat_name(r));
		d->logged_rat = r;
		write_status(d);
	}
	apply_rat_pref(d);
}

static void op_timer(void *ctx, uint32_t ms)
{
	timerfd_arm_ms(((struct ltecm *)ctx)->tfd, ms);
}

static void op_netif(void *ctx, bool up)
{
	struct ltecm *d = ctx;
	char *argv[] = { (char *)d->hook, up ? "up" : "down", NULL };
	pid_t pid;

	if (posix_spawn(&pid, d->hook, NULL, NULL, argv, environ) != 0) {
		LOG_E("hook %s failed to start", d->hook);
		return;
	}
	waitpid(pid, NULL, 0);
}

static void op_notify(void *ctx, lte_state_t s, const char *why)
{
	struct ltecm *d = ctx;

	LOG_I("state %s (%s) on %s", lte_state_name(s), why, rat_name(d->rat.serving));
	/* Fresh modem session: forget responses to commands that timed out. */
	if (s == LTE_MODEM_INIT)
		at_chan_reset(&d->chan);
	/* Bearer would not come up on this RAT: let the policy steer away. */
	if (d->prev_state == LTE_PDN_CONNECTING && s == LTE_BACKOFF) {
		rat_policy_on_data_fail(&d->rat, now_ms());
		d->prev_state = s;
		apply_rat_pref(d);
	}
	d->prev_state = s;
	write_status(d);
}

static const struct lte_ops ops = {
	.send = op_send, .start_timer = op_timer, .netif = op_netif, .notify = op_notify,
};

static int reg_domain(const char *line)
{
	if (at_has_prefix(line, "+CGREG"))
		return RAT_DOM_PS;
	if (at_has_prefix(line, "+CEREG"))
		return RAT_DOM_EPS;
	if (at_has_prefix(line, "+C5GREG"))
		return RAT_DOM_5GS;
	return -1; /* +CREG is circuit-switched only: not a data bearer */
}

/*
 * lte_sm sees the combined state over all domains: 4G lost but 3G up is
 * still up. During an inter-RAT change the modem may report the old
 * domain lost before the new one registers, so a loss is only passed on
 * if it is still true at the next poll tick.
 */
static void report_reg(struct ltecm *d, bool from_poll)
{
	switch (rat_policy_reg_stat(&d->rat)) {
	case AT_REG_HOME:
		d->lost_pending = false;
		lte_sm_handle(&d->sm, LTE_EV_REG_HOME);
		break;
	case AT_REG_ROAMING:
		d->lost_pending = false;
		lte_sm_handle(&d->sm, LTE_EV_REG_ROAMING);
		break;
	case AT_REG_DENIED:
		d->lost_pending = false;
		lte_sm_handle(&d->sm, LTE_EV_REG_DENIED);
		break;
	default:
		if (!from_poll && d->sm.state == LTE_CONNECTED) {
			d->lost_pending = true;
			break;
		}
		d->lost_pending = false;
		lte_sm_handle(&d->sm, LTE_EV_REG_LOST);
		break;
	}
}

static void on_final(struct ltecm *d, bool ok, const char *line)
{
	at_owner_t who = at_chan_pop(&d->chan);
	int v;

	if (!ok && at_parse_cme_error(line, &v) == 0)
		LOG_W("CME error %d", v);
	if (who == AT_OWNER_AUX) {
		if (!ok)
			LOG_D("status/RAT command rejected");
		return;
	}
	lte_sm_handle(&d->sm, ok ? LTE_EV_AT_OK : LTE_EV_AT_ERROR);
}

static void on_line(void *ctx, const char *line)
{
	struct ltecm *d = ctx;
	struct at_cesq q;
	int stat, act, mode, dom, v;
	uint64_t now = now_ms();

	LOG_D("< %s", line);
	switch (at_classify(line)) {
	case AT_LINE_OK:
		on_final(d, true, line);
		return;
	case AT_LINE_ERROR:
		on_final(d, false, line);
		return;
	case AT_LINE_INFO:
		break;
	default:
		return;
	}

	if ((v = at_parse_cpin_ready(line)) >= 0) {
		lte_sm_handle(&d->sm, v ? LTE_EV_SIM_READY : LTE_EV_SIM_NOT_READY);
	} else if ((dom = reg_domain(line)) >= 0 &&
		   at_parse_reg_act(line, false, &stat, &act) == 0) {
		rat_policy_on_reg(&d->rat, (rat_domain_t)dom, stat, act, now);
		report_reg(d, false);
		rat_changed(d);
	} else if (at_parse_cops(line, &mode, &act) == 0) {
		rat_policy_on_cops(&d->rat, act, now);
		rat_changed(d);
	} else if (at_parse_cesq(line, &q) == 0) {
		rat_policy_on_cesq(&d->rat, &q, now);
		rat_changed(d);
	}
}

/* Periodic: policy timers, then ask for serving RAT and signal. */
static void on_poll(struct ltecm *d)
{
	lte_state_t s = d->sm.state;

	if (d->lost_pending)
		report_reg(d, true);
	rat_policy_tick(&d->rat, now_ms());
	rat_changed(d);
	if (s != LTE_SEARCHING && s != LTE_PDN_CONNECTING && s != LTE_CONNECTED)
		return;
	if (at_chan_pending(&d->chan, AT_OWNER_AUX))
		return; /* previous poll still unanswered */
	write_cmd(d, "AT+COPS?", AT_OWNER_AUX);
	write_cmd(d, "AT+CESQ", AT_OWNER_AUX);
}

int main(int argc, char **argv)
{
	struct lte_cfg cfg = {
		.at_timeout_ms = 5000, .search_timeout_ms = 180000, .max_radio_cycles = 3,
	};
	struct rat_policy_cfg rcfg = {
		.allowed = RAT_BIT_3G | RAT_BIT_4G | RAT_BIT_5G,
	};
	struct ltecm d = { .hook = "/etc/ltecm/netif-hook", .vendor = RAT_VENDOR_3GPP };
	const char *dev = "/dev/ttyUSB2";
	unsigned int poll_s = 10;
	char m[24];
	rat_mask_t pref;
	sigset_t mask;
	int sfd, opt;

	while ((opt = getopt(argc, argv, "d:a:rH:R:V:P:S:")) != -1) {
		switch (opt) {
		case 'd': dev = optarg; break;
		case 'a': strncpy(cfg.apn, optarg, sizeof(cfg.apn) - 1); break;
		case 'r': cfg.roaming_allowed = true; break;
		case 'H': d.hook = optarg; break;
		case 'R':
			if (rat_mask_parse(optarg, &rcfg.allowed) < 0) {
				fprintf(stderr, "bad RAT list '%s' (e.g. 3g,4g,5g)\n", optarg);
				return 2;
			}
			break;
		case 'V':
			if (rat_vendor_parse(optarg, &d.vendor) < 0) {
				fprintf(stderr, "bad vendor '%s' (3gpp|quectel)\n", optarg);
				return 2;
			}
			break;
		case 'P': poll_s = (unsigned int)strtoul(optarg, NULL, 10); break;
		case 'S': d.status_path = optarg; break;
		default:
			fprintf(stderr, "usage: %s -d tty -a apn [-r] [-H hook] [-R 2g,3g,4g,5g]"
				" [-V 3gpp|quectel] [-P poll_s] [-S status_file]\n", argv[0]);
			return 2;
		}
	}
	if (!poll_s)
		poll_s = 10;
	openlog("ltecm", LOG_PID, LOG_DAEMON);

	d.tty = tty_open_raw(dev, 115200);
	if (d.tty < 0) {
		LOG_E("open %s: %s", dev, strerror(errno));
		return 1;
	}
	d.tfd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
	d.pfd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
	{
		struct itimerspec its = {
			.it_value = { .tv_sec = poll_s }, .it_interval = { .tv_sec = poll_s },
		};

		timerfd_settime(d.pfd, 0, &its, NULL);
	}
	sigemptyset(&mask);
	sigaddset(&mask, SIGTERM);
	sigaddset(&mask, SIGINT);
	sigprocmask(SIG_BLOCK, &mask, NULL);
	sfd = signalfd(-1, &mask, SFD_CLOEXEC);

	at_reader_init(&d.rd, on_line, &d);
	at_chan_reset(&d.chan);
	rat_policy_init(&d.rat, &rcfg);
	cfg.urc_ps = rcfg.allowed & (RAT_BIT_2G | RAT_BIT_3G);
	cfg.urc_5gs = rcfg.allowed & RAT_BIT_5G;
	if (rat_policy_take_pref(&d.rat, &pref))
		rat_pref_cmd(d.vendor, pref, cfg.rat_cmd, sizeof(cfg.rat_cmd));
	LOG_I("allowed RATs %s, preference via %s",
	      rat_mask_str(rcfg.allowed, m, sizeof(m)),
	      d.vendor == RAT_VENDOR_QUECTEL ? "QNWPREFCFG" : "WS46");
	lte_sm_init(&d.sm, &cfg, &ops, &d);
	lte_sm_handle(&d.sm, LTE_EV_START);

	for (;;) {
		struct pollfd p[4] = {
			{ .fd = d.tty, .events = POLLIN },
			{ .fd = d.tfd, .events = POLLIN },
			{ .fd = sfd, .events = POLLIN },
			{ .fd = d.pfd, .events = POLLIN },
		};
		char buf[256];
		ssize_t n;

		if (poll(p, 4, -1) < 0) {
			if (errno == EINTR)
				continue;
			break;
		}
		if (p[0].revents & (POLLHUP | POLLERR)) {
			LOG_E("modem port went away (USB re-enumeration?)");
			break; /* systemd restarts us once the port is back */
		}
		if (p[0].revents & POLLIN && (n = read(d.tty, buf, sizeof(buf))) > 0)
			at_reader_feed(&d.rd, buf, (size_t)n);
		if (p[1].revents & POLLIN) {
			uint64_t exp;

			if (read(d.tfd, &exp, sizeof(exp)) == sizeof(exp))
				lte_sm_handle(&d.sm, LTE_EV_TIMER);
		}
		if (p[3].revents & POLLIN) {
			uint64_t exp;

			if (read(d.pfd, &exp, sizeof(exp)) == sizeof(exp))
				on_poll(&d);
		}
		if (p[2].revents & POLLIN) {
			lte_sm_handle(&d.sm, LTE_EV_STOP);
			break;
		}
	}
	close(d.tty);
	return 0;
}
