#!/usr/bin/env python3
"""Simulated wpa_supplicant for end-to-end tests.

Serves the real control-interface protocol on a UNIX datagram socket
(<ctrl-dir>/<iface>) and simulates a radio environment of access points.
Enough of wpa_supplicant's behaviour is modelled for wifimgrd to be tested
unmodified: scans, network configuration, association with key checking,
unsolicited events for ATTACHed clients, link loss and automatic
reassociation, and TERMINATING.

Test-only commands (sent by the test harness on the same socket):
  TEST_SET_APS <json list>   replace the simulated access points
  TEST_TERMINATE             send CTRL-EVENT-TERMINATING and exit
  TEST_GET_NETWORK           JSON of the currently configured network

An AP is {"ssid", "bssid", "freq", "signal", "flags", "psk"?, "hidden"?}.
"""

import argparse
import heapq
import json
import os
import select
import socket
import sys
import time


def printf_encode(raw: bytes) -> str:
    out = []
    for b in raw:
        c = chr(b)
        if c == '"':
            out.append('\\"')
        elif c == "\\":
            out.append("\\\\")
        elif 32 <= b < 127:
            out.append(c)
        else:
            out.append("\\x%02x" % b)
    return "".join(out)


def unquote(v: str):
    """wpa_supplicant config string: "text" or hex."""
    if len(v) >= 2 and v[0] == '"' and v[-1] == '"':
        return v[1:-1].encode()
    try:
        return bytes.fromhex(v)
    except ValueError:
        return None


class FakeSupplicant:
    def __init__(self, ctrl_dir, iface, aps):
        os.makedirs(ctrl_dir, exist_ok=True)
        self.path = os.path.join(ctrl_dir, iface)
        if os.path.exists(self.path):
            os.unlink(self.path)
        self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)
        self.sock.bind(self.path)
        self.attached = set()
        self.aps = aps
        self.timers = []  # heap of (deadline, seq, fn)
        self.seq = 0
        self.scanning = False
        self.probe_ssids = []
        self.networks = {}
        self.next_id = 0
        self.selected = None
        self.connected_ap = None
        self.disconnected = False
        self.auth_failures = 0
        self.not_found = 0
        self.running = True

    # ---------------------------------------------------------------- utils
    def later(self, delay, fn):
        self.seq += 1
        heapq.heappush(self.timers, (time.monotonic() + delay, self.seq, fn))

    def event(self, msg, level=3):
        data = ("<%d>%s" % (level, msg)).encode()
        for addr in list(self.attached):
            try:
                self.sock.sendto(data, addr)
            except OSError:
                self.attached.discard(addr)

    def visible_aps(self):
        out = []
        for ap in self.aps:
            if ap.get("hidden") and ap["ssid"].encode() not in self.probe_ssids:
                entry = dict(ap, ssid="")
            else:
                entry = ap
            out.append(entry)
        return out

    # ---------------------------------------------------------------- radio
    def finish_scan(self):
        self.scanning = False
        self.event("CTRL-EVENT-SCAN-RESULTS ", level=2)

    def drop_link(self, reason, local=False):
        if self.connected_ap is None:
            return
        bssid = self.connected_ap["bssid"]
        self.connected_ap = None
        self.event("CTRL-EVENT-DISCONNECTED bssid=%s reason=%d%s"
                   % (bssid, reason, " locally_generated=1" if local else ""))

    def try_associate(self, net_id):
        if (not self.running or self.selected != net_id or self.disconnected
                or self.connected_ap is not None):
            return
        net = self.networks.get(net_id)
        if net is None:
            return
        ssid = unquote(net.get("ssid", '""')) or b""
        hidden_ok = net.get("scan_ssid") == "1"
        ap = next((a for a in self.aps if a["ssid"].encode() == ssid
                   and (hidden_ok or not a.get("hidden"))), None)
        if ap is None:
            self.not_found += 1
            if self.not_found == 2:
                self.event("CTRL-EVENT-NETWORK-NOT-FOUND")
            self.later(1.0, lambda: self.try_associate(net_id))
            return
        self.not_found = 0
        key_mgmt = net.get("key_mgmt", "WPA-PSK")
        ap_open = "PSK" not in ap["flags"] and "SAE" not in ap["flags"] and "EAP" not in ap["flags"]
        if (key_mgmt == "NONE") != ap_open:
            self.event("CTRL-EVENT-ASSOC-REJECT bssid=%s status_code=43" % ap["bssid"])
            self.later(1.0, lambda: self.try_associate(net_id))
            return
        if not ap_open:
            given = unquote(net.get("psk") or net.get("sae_password") or '""')
            if given != ap.get("psk", "").encode():
                self.auth_failures += 1
                self.event('CTRL-EVENT-SSID-TEMP-DISABLED id=%d ssid="%s" auth_failures=%d '
                           "duration=10 reason=WRONG_KEY"
                           % (net_id, printf_encode(ssid), self.auth_failures))
                self.later(2.0, lambda: self.try_associate(net_id))
                return
        self.auth_failures = 0
        self.connected_ap = ap
        self.event("CTRL-EVENT-CONNECTED - Connection to %s completed [id=%d id_str=]"
                   % (ap["bssid"], net_id))

    # ---------------------------------------------------------------- commands
    def handle(self, cmd):
        parts = cmd.split(" ")
        op = parts[0]
        if op == "PING":
            return "PONG\n"
        if op == "SCAN":
            if self.scanning:
                return "FAIL-BUSY\n"
            self.probe_ssids = [bytes.fromhex(parts[i + 1])
                                for i in range(1, len(parts) - 1) if parts[i] == "ssid"]
            self.scanning = True
            self.later(0.3, self.finish_scan)
            return "OK\n"
        if op == "SCAN_RESULTS":
            lines = ["bssid / frequency / signal level / flags / ssid"]
            for ap in self.visible_aps():
                lines.append("%s\t%d\t%d\t%s\t%s" % (ap["bssid"], ap["freq"], ap["signal"],
                                                     ap["flags"], printf_encode(ap["ssid"].encode())))
            return "\n".join(lines) + "\n"
        if op == "REMOVE_NETWORK":
            if parts[1] == "all":
                self.networks.clear()
            else:
                self.networks.pop(int(parts[1]), None)
            self.selected = None
            self.drop_link(3, local=True)
            return "OK\n"
        if op == "ADD_NETWORK":
            nid = self.next_id
            self.next_id += 1
            self.networks[nid] = {}
            return "%d\n" % nid
        if op == "SET_NETWORK":
            nid, key = int(parts[1]), parts[2]
            value = cmd.split(" ", 3)[3]
            allowed = {"ssid", "scan_ssid", "priority", "key_mgmt", "psk", "sae_password",
                       "ieee80211w", "eap", "identity", "anonymous_identity", "password",
                       "phase2", "ca_cert"}
            if nid not in self.networks or key not in allowed:
                return "FAIL\n"
            if key == "psk" and not (value.startswith('"') or len(value) == 64):
                return "FAIL\n"
            self.networks[nid][key] = value
            return "OK\n"
        if op == "SELECT_NETWORK":
            nid = int(parts[1])
            if nid not in self.networks:
                return "FAIL\n"
            self.selected = nid
            self.disconnected = False
            self.auth_failures = 0
            self.later(0.3, lambda: self.try_associate(nid))
            return "OK\n"
        if op == "DISCONNECT":
            self.disconnected = True
            self.drop_link(3, local=True)
            return "OK\n"
        if op == "STATUS":
            if self.connected_ap:
                ap = self.connected_ap
                return ("bssid=%s\nfreq=%d\nssid=%s\nid=%d\nmode=station\nkey_mgmt=WPA2-PSK\n"
                        "wpa_state=COMPLETED\naddress=02:00:00:00:01:00\n"
                        % (ap["bssid"], ap["freq"], printf_encode(ap["ssid"].encode()),
                           self.selected or 0))
            state = "SCANNING" if self.selected is not None else "DISCONNECTED"
            return "wpa_state=%s\naddress=02:00:00:00:01:00\n" % state
        if op == "SIGNAL_POLL":
            if not self.connected_ap:
                return "FAIL\n"
            ap = self.connected_ap
            return "RSSI=%d\nLINKSPEED=144\nNOISE=9999\nFREQUENCY=%d\n" % (ap["signal"], ap["freq"])
        # ---- test hooks
        if op == "TEST_SET_APS":
            self.aps = json.loads(cmd.split(" ", 1)[1])
            if self.connected_ap and not any(a["bssid"] == self.connected_ap["bssid"] for a in self.aps):
                self.drop_link(4)  # beacon loss
                if self.selected is not None:
                    sel = self.selected
                    self.later(1.0, lambda: self.try_associate(sel))  # wpa_s keeps trying
            return "OK\n"
        if op == "TEST_GET_NETWORK":
            net = self.networks.get(self.selected, {}) if self.selected is not None else {}
            return json.dumps(net) + "\n"
        if op == "TEST_TERMINATE":
            self.event("CTRL-EVENT-TERMINATING", level=2)
            self.running = False
            return "OK\n"
        return "UNKNOWN COMMAND\n"

    def serve(self):
        while self.running:
            timeout = None
            if self.timers:
                timeout = max(0.0, self.timers[0][0] - time.monotonic())
            r, _, _ = select.select([self.sock], [], [], timeout)
            if r:
                data, addr = self.sock.recvfrom(4096)
                cmd = data.decode(errors="replace").strip()
                if cmd == "ATTACH":
                    self.attached.add(addr)
                    reply = "OK\n"
                elif cmd == "DETACH":
                    self.attached.discard(addr)
                    reply = "OK\n"
                else:
                    reply = self.handle(cmd)
                try:
                    self.sock.sendto(reply.encode(), addr)
                except OSError:
                    pass
            now = time.monotonic()
            while self.timers and self.timers[0][0] <= now:
                _, _, fn = heapq.heappop(self.timers)
                fn()
        self.sock.close()
        os.unlink(self.path)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ctrl-dir", required=True)
    ap.add_argument("--iface", default="wlan0")
    ap.add_argument("--aps", default="[]", help="JSON list of access points")
    args = ap.parse_args()
    fake = FakeSupplicant(args.ctrl_dir, args.iface, json.loads(args.aps))
    print("ready", flush=True)
    fake.serve()
    return 0


if __name__ == "__main__":
    sys.exit(main())
