// SPDX-License-Identifier: MIT
/*
 * btmgr - drives bt_reconnect against BlueZ over D-Bus.
 *
 *   btmgr -d AA:BB:CC:DD:EE:FF [-a hci0] [-i /run/tcu/ignition]
 *
 * BlueZ's own [Policy] ReconnectAttempts handles the simple case; btmgr
 * owns the cases it cannot know about (ignition, eCall, user intent).
 */
#include <gio/gio.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "bt_reconnect.h"
#include "tcu_log.h"

#define BLUEZ "org.bluez"
#define ECALL_FLAG "/run/ecalld.active"

struct btmgr {
	GDBusConnection *bus;
	char adapter_path[64];
	char device_path[128];
	const char *ignition_path;
	struct btr sm;
	guint timer;
	GCancellable *pending;
	btr_disc_reason_t last_reason;
	bool ignition, ecall;
};

static void op_start_timer(void *ctx, uint32_t ms);

static btr_error_t map_error(const GError *err)
{
	g_autofree char *name = g_dbus_error_get_remote_error(err);
	const char *msg = err->message ? err->message : "";

	if (!name)
		return BTR_ERR_OTHER;
	if (!strcmp(name, "org.bluez.Error.InProgress") ||
	    !strcmp(name, "org.bluez.Error.AlreadyConnected"))
		return BTR_ERR_IN_PROGRESS;
	if (strstr(name, "Authentication") || strstr(msg, "key-missing"))
		return BTR_ERR_AUTH;
	if (strstr(msg, "page-timeout") || strstr(msg, "Page Timeout") ||
	    !strcmp(name, "org.bluez.Error.Failed"))
		return BTR_ERR_PAGE_TIMEOUT;
	if (!strcmp(name, "org.bluez.Error.NotReady"))
		return BTR_ERR_BUSY;
	return BTR_ERR_OTHER;
}

static void connect_done(GObject *src, GAsyncResult *res, gpointer data)
{
	struct btmgr *m = data;
	g_autoptr(GError) err = NULL;
	g_autoptr(GVariant) ret = g_dbus_connection_call_finish(G_DBUS_CONNECTION(src), res, &err);

	if (err && g_error_matches(err, G_IO_ERROR, G_IO_ERROR_CANCELLED))
		return; /* we dropped this page on purpose; m may be reused */
	g_clear_object(&m->pending);
	if (err) {
		LOG_W("Connect failed: %s", err->message);
		btr_handle(&m->sm, BTR_EV_CONNECT_FAIL, map_error(err));
	} else {
		btr_handle(&m->sm, BTR_EV_CONNECT_OK, 0);
	}
}

static void op_connect(void *ctx)
{
	struct btmgr *m = ctx;

	g_clear_object(&m->pending);
	m->pending = g_cancellable_new();
	g_dbus_connection_call(m->bus, BLUEZ, m->device_path, "org.bluez.Device1", "Connect",
			       NULL, NULL, G_DBUS_CALL_FLAGS_NONE, 30000, m->pending,
			       connect_done, m);
}

static void op_cancel(void *ctx)
{
	struct btmgr *m = ctx;

	/* Only forget our call; never Disconnect(), the phone may have won. */
	if (m->pending) {
		g_cancellable_cancel(m->pending);
		g_clear_object(&m->pending);
	}
}

static gboolean timer_fired(gpointer data)
{
	struct btmgr *m = data;

	m->timer = 0;
	btr_handle(&m->sm, BTR_EV_TIMER, 0);
	return G_SOURCE_REMOVE;
}

static void op_start_timer(void *ctx, uint32_t ms)
{
	struct btmgr *m = ctx;

	if (m->timer)
		g_source_remove(m->timer);
	m->timer = ms ? g_timeout_add(ms, timer_fired, m) : 0;
}

static void op_notify(void *ctx, btr_state_t s, const char *why)
{
	(void)ctx;
	LOG_I("reconnect %s (%s)", btr_state_name(s), why);
}

static const struct btr_ops ops = {
	.connect = op_connect, .cancel = op_cancel,
	.start_timer = op_start_timer, .notify = op_notify,
};

static void on_props(struct btmgr *m, const char *path, const char *iface, GVariant *changed)
{
	gboolean v;

	if (!strcmp(iface, "org.bluez.Adapter1") && !strcmp(path, m->adapter_path) &&
	    g_variant_lookup(changed, "Powered", "b", &v)) {
		btr_handle(&m->sm, v ? BTR_EV_ADAPTER_ON : BTR_EV_ADAPTER_OFF, 0);
	} else if (!strcmp(iface, "org.bluez.Device1") && !strcmp(path, m->device_path)) {
		if (g_variant_lookup(changed, "Connected", "b", &v)) {
			btr_handle(&m->sm, v ? BTR_EV_CONNECTED : BTR_EV_DISCONNECTED, m->last_reason);
			m->last_reason = BTR_DISC_LINK_LOSS;
		}
		if (g_variant_lookup(changed, "Paired", "b", &v) && v)
			btr_handle(&m->sm, BTR_EV_PAIRED, 0);
	}
}

static void on_signal(GDBusConnection *c, const char *sender, const char *path,
		      const char *iface, const char *signal, GVariant *params, gpointer data)
{
	struct btmgr *m = data;
	(void)c; (void)sender; (void)iface;

	if (!strcmp(signal, "PropertiesChanged")) {
		const char *if_name;
		g_autoptr(GVariant) changed = NULL;

		g_variant_get(params, "(&s@a{sv}as)", &if_name, &changed, NULL);
		on_props(m, path, if_name, changed);
	} else if (!strcmp(signal, "Disconnected") && !strcmp(path, m->device_path)) {
		/* BlueZ >= 5.69 reports why the link went down; it arrives before
		 * Connected=false. Older BlueZ: treated as link loss. */
		const char *reason;

		g_variant_get(params, "(&s&s)", &reason, NULL);
		if (strstr(reason, "Remote"))
			m->last_reason = BTR_DISC_REMOTE_USER;
		else if (strstr(reason, "Local"))
			m->last_reason = BTR_DISC_LOCAL;
		else
			m->last_reason = BTR_DISC_LINK_LOSS;
	}
}

static bool read_ignition(const char *path)
{
	char c = '0';
	FILE *f = fopen(path, "r");

	if (f) {
		if (fread(&c, 1, 1, f) != 1)
			c = '0';
		fclose(f);
	}
	return c == '1';
}

/* Ignition (GPIO exported by tcu-pmd) and eCall flag, sampled at 1 Hz. */
static gboolean poll_inputs(gpointer data)
{
	struct btmgr *m = data;
	bool ign = read_ignition(m->ignition_path);
	bool ecall = access(ECALL_FLAG, F_OK) == 0;

	if (ign != m->ignition) {
		m->ignition = ign;
		btr_handle(&m->sm, ign ? BTR_EV_IGNITION_ON : BTR_EV_IGNITION_OFF, 0);
	}
	if (ecall != m->ecall) {
		m->ecall = ecall;
		btr_handle(&m->sm, ecall ? BTR_EV_SUSPEND : BTR_EV_RESUME, 0);
	}
	return G_SOURCE_CONTINUE;
}

static bool adapter_powered(struct btmgr *m)
{
	g_autoptr(GVariant) r = g_dbus_connection_call_sync(
		m->bus, BLUEZ, m->adapter_path, "org.freedesktop.DBus.Properties", "Get",
		g_variant_new("(ss)", "org.bluez.Adapter1", "Powered"), G_VARIANT_TYPE("(v)"),
		G_DBUS_CALL_FLAGS_NONE, 2000, NULL, NULL);
	g_autoptr(GVariant) v = NULL;

	if (!r)
		return false;
	g_variant_get(r, "(v)", &v);
	return g_variant_get_boolean(v);
}

int main(int argc, char **argv)
{
	struct btr_cfg cfg = {
		.link_loss_first_ms = 1000, .base_ms = 3000, .max_ms = 60000, .max_attempts = 20,
	};
	struct btmgr m = { .ignition_path = "/run/tcu/ignition", .last_reason = BTR_DISC_LINK_LOSS };
	const char *hci = "hci0", *addr = NULL;
	char devpart[32];
	int opt;

	while ((opt = getopt(argc, argv, "a:d:i:")) != -1) {
		switch (opt) {
		case 'a': hci = optarg; break;
		case 'd': addr = optarg; break;
		case 'i': m.ignition_path = optarg; break;
		default: break;
		}
	}
	if (!addr || strlen(addr) != 17) {
		fprintf(stderr, "usage: %s -d AA:BB:CC:DD:EE:FF [-a hci0] [-i ignition]\n", argv[0]);
		return 2;
	}
	openlog("btmgr", LOG_PID, LOG_DAEMON);
	g_strlcpy(devpart, addr, sizeof(devpart));
	g_strdelimit(devpart, ":", '_');
	snprintf(m.adapter_path, sizeof(m.adapter_path), "/org/bluez/%s", hci);
	snprintf(m.device_path, sizeof(m.device_path), "%s/dev_%s", m.adapter_path, devpart);

	m.bus = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, NULL);
	if (!m.bus) {
		LOG_E("no system bus");
		return 1;
	}
	/* Paths are filtered in on_signal; GDBus has no path_namespace match. */
	g_dbus_connection_signal_subscribe(m.bus, BLUEZ, NULL, NULL, NULL, NULL,
					   G_DBUS_SIGNAL_FLAGS_NONE, on_signal, &m, NULL);

	btr_init(&m.sm, &cfg, &ops, &m);
	if (adapter_powered(&m))
		btr_handle(&m.sm, BTR_EV_ADAPTER_ON, 0);
	poll_inputs(&m);
	g_timeout_add_seconds(1, poll_inputs, &m);

	g_main_loop_run(g_main_loop_new(NULL, FALSE));
	return 0;
}
