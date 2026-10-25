// SPDX-License-Identifier: MIT
/*
 * ecalld - in-vehicle eCall daemon.
 *
 *   ecalld -d /dev/ttyUSB3 -v <VIN> [-t] [-c /run/ecalld/ctl]
 *
 * Inputs:
 *   - control socket (SOCK_DGRAM): "manual", "auto", "cancel" from the
 *     SOS button handler and the airbag-ECU CAN bridge
 *   - modem AT port: call progress and MSD-transfer URCs
 *   - /run/tcu/position: "<lat_mas> <lon_mas> <dir> <trusted>" from GNSS
 *
 * While a session is active /run/ecalld/active exists; tcu-pmd holds the
 * system awake and btmgr suspends reconnection attempts while it does.
 */
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/timerfd.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#include "at.h"
#include "ecall_sm.h"
#include "msd.h"
#include "tcu_log.h"
#include "tty.h"

#define ACTIVE_FLAG "/run/ecalld/active"
#define POSITION_FILE "/run/tcu/position"

/*
 * Modem adaptation layer. 3GPP TS 27.007 +CECALL starts the session;
 * MSD upload and in-band modem progress are vendor commands/URCs, so
 * they are collected here and set per modem when porting.
 */
static const struct {
	const char *dial_fmt;   /* %d = 27.007 type: 0 test, 2 manual, 3 auto */
	const char *msd_fmt;    /* %s = MSD as hex */
	const char *urc_connected;
	const char *urc_msd_ack;
	const char *urc_msd_request;
} modem = {
	.dial_fmt = "AT+CECALL=%d",
	.msd_fmt = "AT+VNDMSD=\"%s\"",          /* placeholder: vendor specific */
	.urc_connected = "+VNDECALL: CONNECTED",
	.urc_msd_ack = "+VNDECALL: AL-ACK",
	.urc_msd_request = "+VNDECALL: SEND-MSD",
};

struct ecalld {
	int tty, tfd, ctl;
	struct ecall_sm sm;
	struct at_reader rd;
	struct msd msd;
	struct msd prev[2]; /* last two fixes for N1/N2 deltas */
	unsigned int fixes;
};

static void load_position(struct ecalld *d)
{
	FILE *f = fopen(POSITION_FILE, "r");
	int lat, lon, dir, trusted;

	d->msd.timestamp = (uint32_t)time(NULL);
	if (!f) {
		d->msd.position_trusted = false;
		return;
	}
	if (fscanf(f, "%d %d %d %d", &lat, &lon, &dir, &trusted) == 4) {
		d->prev[1] = d->prev[0];
		d->prev[0] = d->msd;
		d->msd.latitude_mas = lat;
		d->msd.longitude_mas = lon;
		d->msd.direction = (uint8_t)((dir >= 0 && dir < 360) ? dir / 2 : 255);
		d->msd.position_trusted = trusted != 0;
		d->fixes++;
	}
	fclose(f);
}

static int16_t clamp_delta(int32_t mas)
{
	int32_t v = mas / 100;

	return (int16_t)(v < -512 ? -512 : v > 511 ? 511 : v);
}

static void op_dial(void *ctx, bool automatic, bool test)
{
	struct ecalld *d = ctx;
	char cmd[64];

	d->msd.automatic_activation = automatic;
	d->msd.test_call = test;
	snprintf(cmd, sizeof(cmd), modem.dial_fmt, test ? 0 : automatic ? 3 : 2);
	tty_write_cmd(d->tty, cmd);
}

static void op_send_msd(void *ctx, uint8_t id)
{
	struct ecalld *d = ctx;
	uint8_t raw[MSD_MAX_BYTES];
	char hex[2 * MSD_MAX_BYTES + 1], cmd[sizeof(hex) + 32];
	int len;

	load_position(d);
	d->msd.message_id = id;
	d->msd.n1_lat_delta = d->msd.n1_lon_delta = 0;
	d->msd.n2_lat_delta = d->msd.n2_lon_delta = 0;
	if (d->fixes >= 2) {
		d->msd.n1_lat_delta = clamp_delta(d->prev[0].latitude_mas - d->msd.latitude_mas);
		d->msd.n1_lon_delta = clamp_delta(d->prev[0].longitude_mas - d->msd.longitude_mas);
	}
	if (d->fixes >= 3) {
		d->msd.n2_lat_delta = clamp_delta(d->prev[1].latitude_mas - d->prev[0].latitude_mas);
		d->msd.n2_lon_delta = clamp_delta(d->prev[1].longitude_mas - d->prev[0].longitude_mas);
	}

	len = msd_encode(&d->msd, raw, sizeof(raw));
	if (len < 0) {
		LOG_E("MSD encode failed");
		return;
	}
	for (int i = 0; i < len; i++)
		snprintf(hex + 2 * i, 3, "%02X", raw[i]);
	snprintf(cmd, sizeof(cmd), modem.msd_fmt, hex);
	tty_write_cmd(d->tty, cmd);
	LOG_I("MSD #%u sent (%d bytes)", id, len);
}

static void op_answer(void *ctx) { tty_write_cmd(((struct ecalld *)ctx)->tty, "ATA"); }
static void op_hangup(void *ctx) { tty_write_cmd(((struct ecalld *)ctx)->tty, "ATH"); }
static void op_timer(void *ctx, uint32_t ms) { timerfd_arm_ms(((struct ecalld *)ctx)->tfd, ms); }
static void op_stop_timer(void *ctx) { timerfd_arm_ms(((struct ecalld *)ctx)->tfd, 0); }

static void op_audio(void *ctx, bool to_psap)
{
	(void)ctx;
	/* Audio routing (mute infotainment, codec to modem PCM) is owned by the
	 * audio manager; signal it through the active flag consumers. */
	LOG_I("audio %s", to_psap ? "-> PSAP" : "released");
}

static void op_notify(void *ctx, ecall_state_t s, const char *why)
{
	(void)ctx;
	LOG_I("eCall %s (%s)", ecall_state_name(s), why);
	if (s == ECALL_IDLE) {
		unlink(ACTIVE_FLAG);
	} else {
		int fd = open(ACTIVE_FLAG, O_CREAT | O_WRONLY | O_CLOEXEC, 0644);

		if (fd >= 0)
			close(fd);
	}
}

static const struct ecall_ops ops = {
	.dial = op_dial, .send_msd = op_send_msd, .answer = op_answer,
	.hangup = op_hangup, .start_timer = op_timer, .stop_timer = op_stop_timer,
	.audio_route = op_audio, .notify = op_notify,
};

static void on_line(void *ctx, const char *line)
{
	struct ecalld *d = ctx;

	switch (at_classify(line)) {
	case AT_LINE_NO_CARRIER:
		ecall_sm_handle(&d->sm, ECALL_EV_CALL_ENDED);
		return;
	case AT_LINE_BUSY:
	case AT_LINE_NO_ANSWER:
		ecall_sm_handle(&d->sm, ECALL_EV_CALL_FAILED);
		return;
	case AT_LINE_ERROR:
		if (d->sm.state == ECALL_DIALING)
			ecall_sm_handle(&d->sm, ECALL_EV_CALL_FAILED);
		return;
	case AT_LINE_RING:
		ecall_sm_handle(&d->sm, ECALL_EV_INCOMING_CALL);
		return;
	default:
		break;
	}
	if (strcmp(line, modem.urc_connected) == 0)
		ecall_sm_handle(&d->sm, ECALL_EV_CALL_CONNECTED);
	else if (strcmp(line, modem.urc_msd_ack) == 0)
		ecall_sm_handle(&d->sm, ECALL_EV_MSD_ACK);
	else if (strcmp(line, modem.urc_msd_request) == 0)
		ecall_sm_handle(&d->sm, ECALL_EV_MSD_REQUEST);
}

static void on_ctl(struct ecalld *d)
{
	char buf[32];
	ssize_t n = recv(d->ctl, buf, sizeof(buf) - 1, 0);

	if (n <= 0)
		return;
	buf[n] = '\0';
	buf[strcspn(buf, "\r\n")] = '\0';
	if (!strcmp(buf, "manual"))
		ecall_sm_handle(&d->sm, ECALL_EV_TRIGGER_MANUAL);
	else if (!strcmp(buf, "auto"))
		ecall_sm_handle(&d->sm, ECALL_EV_TRIGGER_AUTO);
	else if (!strcmp(buf, "cancel"))
		ecall_sm_handle(&d->sm, ECALL_EV_CANCEL);
	else
		LOG_W("unknown control command '%s'", buf);
}

static int open_ctl(const char *path)
{
	struct sockaddr_un sa = { .sun_family = AF_UNIX };
	int fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);

	if (fd < 0 || strlen(path) >= sizeof(sa.sun_path))
		return -1;
	strcpy(sa.sun_path, path);
	unlink(path);
	if (bind(fd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
		close(fd);
		return -1;
	}
	chmod(path, 0660); /* group "ecall": SOS button + CAN bridge */
	return fd;
}

int main(int argc, char **argv)
{
	struct ecall_cfg cfg = {
		.cancel_window_ms = 3000, .redial_delay_ms = 5000,
		.max_dial_attempts = 10, .msd_ack_timeout_ms = 20000,
		.callback_window_ms = 60 * 60 * 1000,
	};
	struct ecalld d = { 0 };
	const char *dev = "/dev/ttyUSB3", *ctl = "/run/ecalld/ctl";
	int opt;

	d.msd.vehicle_type = MSD_VEH_HEAVY_DUTY_N3;
	d.msd.propulsion = MSD_FUEL_DIESEL;
	d.msd.direction = 255;
	while ((opt = getopt(argc, argv, "d:v:tc:")) != -1) {
		switch (opt) {
		case 'd': dev = optarg; break;
		case 'v': strncpy(d.msd.vin, optarg, MSD_VIN_LEN); break;
		case 't': cfg.test_call = true; break;
		case 'c': ctl = optarg; break;
		default:
			fprintf(stderr, "usage: %s -d tty -v VIN [-t] [-c sock]\n", argv[0]);
			return 2;
		}
	}
	openlog("ecalld", LOG_PID, LOG_DAEMON);
	if (!msd_vin_valid(d.msd.vin)) {
		LOG_E("invalid VIN");
		return 2;
	}
	d.tty = tty_open_raw(dev, 115200);
	d.ctl = open_ctl(ctl);
	d.tfd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
	if (d.tty < 0 || d.ctl < 0 || d.tfd < 0) {
		LOG_E("setup failed: %s", strerror(errno));
		return 1;
	}
	signal(SIGPIPE, SIG_IGN);
	at_reader_init(&d.rd, on_line, &d);
	ecall_sm_init(&d.sm, &cfg, &ops, &d);
	unlink(ACTIVE_FLAG);

	for (;;) {
		struct pollfd p[3] = {
			{ .fd = d.tty, .events = POLLIN },
			{ .fd = d.tfd, .events = POLLIN },
			{ .fd = d.ctl, .events = POLLIN },
		};
		char buf[256];
		ssize_t n;

		if (poll(p, 3, -1) < 0) {
			if (errno == EINTR)
				continue;
			return 1;
		}
		if (p[0].revents & POLLIN && (n = read(d.tty, buf, sizeof(buf))) > 0)
			at_reader_feed(&d.rd, buf, (size_t)n);
		if (p[1].revents & POLLIN) {
			uint64_t exp;

			if (read(d.tfd, &exp, sizeof(exp)) == sizeof(exp))
				ecall_sm_handle(&d.sm, ECALL_EV_TIMER);
		}
		if (p[2].revents & POLLIN)
			on_ctl(&d);
	}
}
