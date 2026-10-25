// SPDX-License-Identifier: MIT
/*
 * ltecm - LTE connection manager daemon.
 *
 *   ltecm -d /dev/ttyUSB2 -a <apn> [-r] [-H /etc/ltecm/netif-hook]
 *
 * Owns the modem's AT control port, drives lte_sm, and calls a hook
 * script ("up"/"down") to configure wwan0 (DHCP, routes, DNS).
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
#include <unistd.h>

#include "at.h"
#include "lte_sm.h"
#include "tcu_log.h"
#include "tty.h"

extern char **environ;

struct ltecm {
	int tty;
	int tfd;
	const char *hook;
	struct lte_sm sm;
	struct at_reader rd;
};

static void op_send(void *ctx, const char *cmd)
{
	struct ltecm *d = ctx;

	LOG_D("> %s", cmd);
	if (tty_write_cmd(d->tty, cmd) < 0)
		LOG_E("write failed: %s", strerror(errno));
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
	(void)ctx;
	LOG_I("state %s (%s)", lte_state_name(s), why);
}

static const struct lte_ops ops = {
	.send = op_send, .start_timer = op_timer, .netif = op_netif, .notify = op_notify,
};

static void on_line(void *ctx, const char *line)
{
	struct ltecm *d = ctx;
	int stat, v;

	LOG_D("< %s", line);
	switch (at_classify(line)) {
	case AT_LINE_OK:
		lte_sm_handle(&d->sm, LTE_EV_AT_OK);
		return;
	case AT_LINE_ERROR:
		if (at_parse_cme_error(line, &v) == 0)
			LOG_W("CME error %d", v);
		lte_sm_handle(&d->sm, LTE_EV_AT_ERROR);
		return;
	case AT_LINE_INFO:
		break;
	default:
		return;
	}

	if ((v = at_parse_cpin_ready(line)) >= 0) {
		lte_sm_handle(&d->sm, v ? LTE_EV_SIM_READY : LTE_EV_SIM_NOT_READY);
	} else if (at_parse_reg(line, false, &stat) == 0) {
		switch (stat) {
		case AT_REG_HOME: lte_sm_handle(&d->sm, LTE_EV_REG_HOME); break;
		case AT_REG_ROAMING: lte_sm_handle(&d->sm, LTE_EV_REG_ROAMING); break;
		case AT_REG_DENIED: lte_sm_handle(&d->sm, LTE_EV_REG_DENIED); break;
		default: lte_sm_handle(&d->sm, LTE_EV_REG_LOST); break;
		}
	}
}

int main(int argc, char **argv)
{
	struct lte_cfg cfg = {
		.at_timeout_ms = 5000, .search_timeout_ms = 180000, .max_radio_cycles = 3,
	};
	struct ltecm d = { .hook = "/etc/ltecm/netif-hook" };
	const char *dev = "/dev/ttyUSB2";
	sigset_t mask;
	int sfd, opt;

	while ((opt = getopt(argc, argv, "d:a:rH:")) != -1) {
		switch (opt) {
		case 'd': dev = optarg; break;
		case 'a': strncpy(cfg.apn, optarg, sizeof(cfg.apn) - 1); break;
		case 'r': cfg.roaming_allowed = true; break;
		case 'H': d.hook = optarg; break;
		default:
			fprintf(stderr, "usage: %s -d tty -a apn [-r] [-H hook]\n", argv[0]);
			return 2;
		}
	}
	openlog("ltecm", LOG_PID, LOG_DAEMON);

	d.tty = tty_open_raw(dev, 115200);
	if (d.tty < 0) {
		LOG_E("open %s: %s", dev, strerror(errno));
		return 1;
	}
	d.tfd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
	sigemptyset(&mask);
	sigaddset(&mask, SIGTERM);
	sigaddset(&mask, SIGINT);
	sigprocmask(SIG_BLOCK, &mask, NULL);
	sfd = signalfd(-1, &mask, SFD_CLOEXEC);

	at_reader_init(&d.rd, on_line, &d);
	lte_sm_init(&d.sm, &cfg, &ops, &d);
	lte_sm_handle(&d.sm, LTE_EV_START);

	for (;;) {
		struct pollfd p[3] = {
			{ .fd = d.tty, .events = POLLIN },
			{ .fd = d.tfd, .events = POLLIN },
			{ .fd = sfd, .events = POLLIN },
		};
		char buf[256];
		ssize_t n;

		if (poll(p, 3, -1) < 0) {
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
		if (p[2].revents & POLLIN) {
			lte_sm_handle(&d.sm, LTE_EV_STOP);
			break;
		}
	}
	close(d.tty);
	return 0;
}
