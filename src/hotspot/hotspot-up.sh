#!/bin/sh
# SPDX-License-Identifier: MIT
# Bring the passenger hotspot up or down: SoftAP (hostapd) on ap0, DHCP/DNS
# (dnsmasq), and the nftables policy that tethers it to the LTE bearer.
#
#   tcu-hotspot-up start|stop|reload
set -eu

PHY_IF=${PHY_IF:-wlan0}
AP_IF=${AP_IF:-ap0}
AP_ADDR=${AP_ADDR:-192.168.50.1/24}
CONF_DIR=${CONF_DIR:-/etc/tcu}
POLICY=/usr/libexec/tcu-hotspot/hotspot_policy.py
RUN=/run/tcu-hotspot

start() {
	mkdir -p "$RUN"
	# Separate virtual interface so the station side (depot Wi-Fi for OTA)
	# keeps running on wlan0 concurrently.
	if ! ip link show "$AP_IF" >/dev/null 2>&1; then
		iw dev "$PHY_IF" interface add "$AP_IF" type __ap
	fi
	ip addr flush dev "$AP_IF"
	ip addr add "$AP_ADDR" dev "$AP_IF"
	ip link set "$AP_IF" up

	sysctl -qw net.ipv4.ip_forward=1
	# Policy before hostapd: no client may associate into an open forwarder.
	python3 "$POLICY" apply --policy "$CONF_DIR/hotspot-policy.json"

	dnsmasq --conf-file="$CONF_DIR/dnsmasq-hotspot.conf" --pid-file="$RUN/dnsmasq.pid"
	hostapd -B -P "$RUN/hostapd.pid" "$CONF_DIR/hostapd-ap0.conf"
}

stop() {
	for p in hostapd dnsmasq; do
		[ -f "$RUN/$p.pid" ] && kill "$(cat "$RUN/$p.pid")" 2>/dev/null || true
		rm -f "$RUN/$p.pid"
	done
	nft delete table inet tcu_hotspot 2>/dev/null || true
	ip link set "$AP_IF" down 2>/dev/null || true
}

case "${1:-}" in
start) start ;;
stop) stop ;;
reload) python3 "$POLICY" apply --policy "$CONF_DIR/hotspot-policy.json" ;;
*) echo "usage: $0 start|stop|reload" >&2; exit 2 ;;
esac
