#!/usr/bin/env python3
"""End-to-end tests: real wifimgrd and wifictl binaries against the simulated
wpa_supplicant (fake_wpa_supplicant.py) and a fake DHCP client that reports
leases through `wifictl dhcp-event`, exactly like the production udhcpc hook.

Run directly or via ctest:
  python3 tests/e2e/test_e2e.py --wifimgrd build/wifimgrd --wifictl build/wifictl
"""

import argparse
import json
import os
import queue
import shutil
import socket
import stat
import subprocess
import sys
import tempfile
import threading
import time
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
WIFIMGRD = None
WIFICTL = None

DEPOT = {"ssid": "Depot WiFi", "bssid": "02:00:00:00:00:01", "freq": 2437, "signal": -52,
         "flags": "[WPA2-PSK-CCMP][ESS]", "psk": "forklift-42"}
YARD = {"ssid": "Yard", "bssid": "02:00:00:00:00:02", "freq": 5180, "signal": -67,
        "flags": "[WPA2-PSK+SAE-CCMP][ESS]", "psk": "container-77"}
CAFE = {"ssid": "Cafe \"Free\"", "bssid": "02:00:00:00:00:03", "freq": 2412, "signal": -71,
        "flags": "[ESS]"}
HIDDEN = {"ssid": "Stealth", "bssid": "02:00:00:00:00:04", "freq": 2462, "signal": -60,
          "flags": "[WPA2-PSK-CCMP][ESS]", "psk": "invisible-1", "hidden": True}
DEFAULT_APS = [DEPOT, YARD, CAFE, HIDDEN]

LEASE_IP = "192.168.50.23"


def wait_for(pred, timeout=15.0, interval=0.1, what="condition"):
    deadline = time.monotonic() + timeout
    last = None
    while time.monotonic() < deadline:
        last = pred()
        if last:
            return last
        time.sleep(interval)
    raise AssertionError("timed out waiting for %s (last: %r)" % (what, last))


class Env:
    """One isolated instance: fake supplicant + wifimgrd in a temp dir."""

    def __init__(self, aps=None):
        self.dir = tempfile.mkdtemp(prefix="wifimgr-e2e-")
        self.ctrl_dir = os.path.join(self.dir, "wpa")
        self.sock = os.path.join(self.dir, "run", "wifimgr.sock")
        self.profiles = os.path.join(self.dir, "lib", "networks.conf")
        self.dhcp_fail = os.path.join(self.dir, "dhcp_fail")
        self.daemon_log = os.path.join(self.dir, "wifimgrd.log")
        self.aps = aps if aps is not None else DEFAULT_APS
        self.fake = None
        self.daemon = None
        self._write_files()

    def _write_files(self):
        dhcp = os.path.join(self.dir, "fake-dhcp.sh")
        with open(dhcp, "w") as f:
            f.write("#!/bin/sh\n"
                    "# Behaves like `udhcpc -f -n` + hook script.\n"
                    "ctl() { '%s' -s '%s' dhcp-event \"$@\" >/dev/null; }\n"
                    "ctl deconfig\n"
                    "sleep 0.2\n"
                    "if [ -e '%s' ]; then ctl leasefail; exit 1; fi\n"
                    "ctl bound %s\n"
                    "exec sleep 3600\n" % (WIFICTL, self.sock, self.dhcp_fail, LEASE_IP))
        os.chmod(dhcp, 0o755)
        self.config = os.path.join(self.dir, "wifimgr.conf")
        with open(self.config, "w") as f:
            f.write("interface = wlan0\n"
                    "wpa_ctrl_dir = %s\n"
                    "wpa_client_dir = %s\n"
                    "control_socket = %s\n"
                    "profiles = %s\n"
                    "dhcp_command = %s\n"
                    "netlink = false\n"
                    "log_level = debug\n"
                    "scan_interval_min = 1\n"
                    "scan_interval_max = 2\n"
                    "scan_timeout = 5\n"
                    "connect_timeout = 4\n"
                    "dhcp_timeout = 4\n"
                    "signal_poll_interval = 1\n"
                    "blacklist_base = 1\n"
                    "blacklist_max = 2\n"
                    % (self.ctrl_dir, os.path.join(self.dir, "client"), self.sock,
                       self.profiles, dhcp))

    # ---------------------------------------------------------------- processes
    def start_fake(self):
        self.fake = subprocess.Popen(
            [sys.executable, os.path.join(HERE, "fake_wpa_supplicant.py"),
             "--ctrl-dir", self.ctrl_dir, "--aps", json.dumps(self.aps)],
            stdout=subprocess.PIPE, text=True)
        assert self.fake.stdout.readline().strip() == "ready"

    def stop_fake(self, terminate_event=True):
        if self.fake and self.fake.poll() is None:
            if terminate_event:
                self.fake_cmd("TEST_TERMINATE")
            try:
                self.fake.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.fake.kill()
                self.fake.wait()
        if self.fake:
            self.fake.stdout.close()
        self.fake = None

    def start_daemon(self):
        self.log = open(self.daemon_log, "a")
        self.daemon = subprocess.Popen([WIFIMGRD, "-c", self.config], stderr=self.log)
        wait_for(lambda: os.path.exists(self.sock), 5, what="control socket")

    def stop_daemon(self):
        if self.daemon and self.daemon.poll() is None:
            self.daemon.terminate()
            try:
                self.daemon.wait(timeout=10)
            except subprocess.TimeoutExpired:
                self.daemon.kill()
                self.daemon.wait()
            rc = self.daemon.returncode
            self.daemon = None
            self.log.close()
            return rc
        return None

    def close(self):
        self.stop_daemon()
        self.stop_fake(terminate_event=False)
        if os.environ.get("WIFIMGR_E2E_KEEP"):
            print("kept", self.dir)
        else:
            shutil.rmtree(self.dir, ignore_errors=True)

    # ---------------------------------------------------------------- clients
    def ctl(self, *args, json_out=True, timeout=60, stdin=None):
        cmd = [WIFICTL, "-s", self.sock] + (["--json"] if json_out else []) + list(args)
        p = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout, input=stdin)
        out = None
        if json_out and p.stdout.strip():
            out = json.loads(p.stdout.strip().splitlines()[0])
        return p.returncode, out, p.stdout, p.stderr

    def status(self):
        rc, out, _, _ = self.ctl("status")
        assert rc == 0, out
        return out["status"]

    def wait_state(self, state, ssid=None, timeout=15):
        def check():
            s = self.status()
            return s if s["state"] == state and (ssid is None or s["ssid"] == ssid) else None
        return wait_for(check, timeout, what="state %s %s" % (state, ssid or ""))

    def fake_cmd(self, cmd):
        path = os.path.join(self.dir, "fakectl-%d" % time.monotonic_ns())
        s = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)
        s.bind(path)
        s.settimeout(5)
        try:
            s.sendto(cmd.encode(), os.path.join(self.ctrl_dir, "wlan0"))
            return s.recv(4096).decode()
        finally:
            s.close()
            os.unlink(path)

    def log_text(self):
        with open(self.daemon_log) as f:
            return f.read()


class E2E(unittest.TestCase):
    aps = None

    def setUp(self):
        self.env = Env(self.aps)
        self.env.start_fake()
        self.env.start_daemon()

    def _failed(self):
        outcome = getattr(self, "_outcome", None)
        result = getattr(outcome, "result", None)
        if result is None:
            return False
        return any(test is self for test, _ in result.errors + result.failures)

    def tearDown(self):
        if self._failed() or os.environ.get("WIFIMGR_E2E_LOG"):
            sys.stderr.write("---- wifimgrd log ----\n" + self.env.log_text()[-6000:])
        self.env.close()

    # ---------------------------------------------------------------- tests
    def test_version_and_initial_status(self):
        rc, out, _, _ = self.env.ctl("version")
        self.assertEqual(rc, 0)
        self.assertTrue(out["version"])
        s = self.env.wait_state("idle")  # no saved networks: no autoconnect scans
        self.assertEqual(s["saved_networks"], 0)

    def test_scan_lists_networks_and_hides_hidden_ssids(self):
        rc, out, _, _ = self.env.ctl("scan")
        self.assertEqual(rc, 0, out)
        ssids = [r["ssid"] for r in out["results"]]
        self.assertEqual(ssids[0], "Depot WiFi")  # sorted by signal
        self.assertIn("Cafe \"Free\"", ssids)
        self.assertIn("", ssids)  # the hidden AP has no SSID
        self.assertNotIn("Stealth", ssids)
        sec = {r["ssid"]: r["security"] for r in out["results"]}
        self.assertEqual(sec["Yard"], "wpa-psk-sae")
        self.assertEqual(sec["Cafe \"Free\""], "open")
        # Human-readable table.
        rc, _, text, _ = self.env.ctl("results", json_out=False)
        self.assertEqual(rc, 0)
        self.assertIn("Depot WiFi", text)
        self.assertIn("<hidden>", text)

    def test_connect_with_passphrase_obtains_ip_and_persists_securely(self):
        rc, out, _, err = self.env.ctl("connect", "Depot WiFi", "--psk", "forklift-42")
        self.assertEqual(rc, 0, err)
        self.assertEqual(out["status"]["ip"], LEASE_IP)
        s = self.env.status()
        self.assertEqual(s["state"], "connected")
        self.assertEqual(s["bssid"], DEPOT["bssid"])
        self.assertEqual(s["rssi"] if "rssi" in s else -52, -52)

        # wpa_supplicant got exactly what we expect.
        net = json.loads(self.env.fake_cmd("TEST_GET_NETWORK"))
        self.assertEqual(net["ssid"], b"Depot WiFi".hex())
        self.assertEqual(net["psk"], '"forklift-42"')
        self.assertEqual(net["key_mgmt"], "WPA-PSK")

        # Saved, readable only by the owner, secret never returned.
        mode = stat.S_IMODE(os.stat(self.env.profiles).st_mode)
        self.assertEqual(mode, 0o600)
        with open(self.env.profiles) as f:
            self.assertIn("psk=forklift-42", f.read())
        rc, out, raw, _ = self.env.ctl("networks")
        self.assertEqual(out["networks"][0]["status"], "current")
        self.assertNotIn("forklift-42", raw)
        self.assertNotIn("forklift-42", self.env.log_text())

    def test_passphrase_from_stdin(self):
        rc, _, _, err = self.env.ctl("connect", "Yard", "--psk", "-", stdin="container-77\n")
        self.assertEqual(rc, 0, err)
        self.env.wait_state("connected", "Yard")

    def test_wrong_passphrase_is_reported(self):
        rc, _, _, err = self.env.ctl("connect", "Depot WiFi", "--psk", "not-the-key", json_out=False)
        self.assertEqual(rc, 1)
        self.assertIn("authentication failed", err)

    def test_open_network_with_special_characters(self):
        rc, out, _, err = self.env.ctl("connect", "Cafe \"Free\"", "--security", "open")
        self.assertEqual(rc, 0, err)
        net = json.loads(self.env.fake_cmd("TEST_GET_NETWORK"))
        self.assertEqual(net["key_mgmt"], "NONE")

    def test_hidden_network(self):
        rc, _, _, err = self.env.ctl("connect", "Stealth", "--psk", "invisible-1", "--hidden")
        self.assertEqual(rc, 0, err)
        net = json.loads(self.env.fake_cmd("TEST_GET_NETWORK"))
        self.assertEqual(net["scan_ssid"], "1")
        # Hidden profile SSIDs are probed explicitly, so it shows up by name.
        rc, out, _, _ = self.env.ctl("scan")
        self.assertIn("Stealth", [r["ssid"] for r in out["results"]])

    def test_autoconnect_after_restart_uses_saved_profiles(self):
        self.assertEqual(self.env.ctl("add", "Yard", "--psk", "container-77")[0], 0)
        self.env.wait_state("connected", "Yard")
        self.assertEqual(self.env.stop_daemon(), 0)
        self.env.start_daemon()
        s = self.env.wait_state("connected", "Yard")
        self.assertEqual(s["ip"], LEASE_IP)

    def test_priority_selection_and_failover_on_ap_loss(self):
        self.env.ctl("add", "Yard", "--psk", "container-77", "--priority", "1")
        self.env.ctl("add", "Depot WiFi", "--psk", "forklift-42", "--priority", "10")
        self.env.ctl("reconnect")
        self.env.wait_state("connected", "Depot WiFi")
        # Depot AP disappears: link lost -> reassociation times out -> Yard.
        self.env.fake_cmd("TEST_SET_APS " + json.dumps([YARD, CAFE]))
        self.env.wait_state("connected", "Yard", timeout=25)
        # Depot comes back: higher priority wins on the next scan.
        self.env.fake_cmd("TEST_SET_APS " + json.dumps(DEFAULT_APS))
        self.env.ctl("scan")
        self.env.wait_state("connected", "Depot WiFi", timeout=20)

    def test_link_loss_recovers_on_same_network(self):
        self.env.ctl("connect", "Depot WiFi", "--psk", "forklift-42")
        self.env.fake_cmd("TEST_SET_APS " + json.dumps([YARD]))
        self.env.wait_state("connecting", "Depot WiFi", timeout=5)
        self.env.fake_cmd("TEST_SET_APS " + json.dumps(DEFAULT_APS))
        self.env.wait_state("connected", "Depot WiFi", timeout=10)

    def test_refuses_open_ap_impersonating_saved_secure_network(self):
        evil = dict(CAFE, ssid="Depot WiFi", bssid="02:00:00:00:00:66", signal=-30)
        self.env.fake_cmd("TEST_SET_APS " + json.dumps([evil]))
        self.env.ctl("add", "Depot WiFi", "--psk", "forklift-42")
        time.sleep(3)
        s = self.env.status()
        self.assertNotEqual(s["state"], "connected")
        self.assertNotIn('"key_mgmt": "NONE"', self.env.fake_cmd("TEST_GET_NETWORK"))

    def test_supplicant_restart_is_recovered(self):
        self.env.ctl("connect", "Depot WiFi", "--psk", "forklift-42")
        self.env.stop_fake()
        self.env.wait_state("unavailable", timeout=5)
        self.env.start_fake()
        self.env.wait_state("connected", "Depot WiFi", timeout=15)

    def test_supplicant_crash_without_terminating_event(self):
        self.env.ctl("connect", "Depot WiFi", "--psk", "forklift-42")
        self.env.fake.kill()
        self.env.fake.wait()
        self.env.fake.stdout.close()
        self.env.fake = None
        self.env.wait_state("unavailable", timeout=20)  # detected by PING health check
        self.env.start_fake()
        self.env.wait_state("connected", "Depot WiFi", timeout=15)

    def test_dhcp_failure_is_reported(self):
        open(self.env.dhcp_fail, "w").close()
        rc, _, _, err = self.env.ctl("connect", "Depot WiFi", "--psk", "forklift-42",
                                     json_out=False)
        self.assertEqual(rc, 1)
        self.assertIn("DHCP", err)

    def test_disconnect_pauses_and_forget_removes(self):
        self.env.ctl("connect", "Depot WiFi", "--psk", "forklift-42")
        self.assertEqual(self.env.ctl("disconnect")[0], 0)
        s = self.env.wait_state("idle")
        self.assertTrue(s["paused"])
        time.sleep(2)
        self.assertEqual(self.env.status()["state"], "idle")  # stays down
        self.assertEqual(self.env.ctl("forget", "Depot WiFi")[0], 0)
        rc, out, _, _ = self.env.ctl("networks")
        self.assertEqual(out["networks"], [])
        rc, _, _, err = self.env.ctl("forget", "Depot WiFi", json_out=False)
        self.assertEqual(rc, 1)
        self.assertIn("unknown network", err)

    def test_monitor_streams_events(self):
        mon = subprocess.Popen([WIFICTL, "-s", self.env.sock, "--json", "monitor"],
                               stdout=subprocess.PIPE, text=True)
        lines = queue.Queue()
        threading.Thread(target=lambda: [lines.put(l) for l in mon.stdout], daemon=True).start()

        def next_line(timeout=10):
            try:
                return json.loads(lines.get(timeout=timeout))
            except queue.Empty:
                raise AssertionError("no output from wifictl monitor")
        try:
            self.assertTrue(next_line()["ok"])
            self.env.ctl("connect", "Depot WiFi", "--psk", "forklift-42")
            states = []
            deadline = time.monotonic() + 10
            while time.monotonic() < deadline:
                ev = next_line()
                if ev["event"] == "state":
                    states.append(ev["data"]["state"])
                    if ev["data"]["state"] == "connected":
                        break
            self.assertEqual(states[-3:], ["connecting", "obtaining_ip", "connected"])
        finally:
            mon.terminate()
            mon.wait()
            mon.stdout.close()

    def test_bad_requests(self):
        rc, _, _, err = self.env.ctl("connect", "X", "--psk", "short", json_out=False)
        self.assertEqual(rc, 1)
        self.assertIn("passphrase", err)
        rc, _, _, err = self.env.ctl("frobnicate", json_out=False)
        self.assertEqual(rc, 1)
        # Raw protocol abuse does not kill the daemon.
        s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        s.connect(self.env.sock)
        s.sendall(b'connect "unterminated\n' + b"A" * 10000)
        s.close()
        self.assertEqual(self.env.ctl("ping")[0], 0)

    def test_second_instance_refuses_to_start(self):
        p = subprocess.run([WIFIMGRD, "-c", self.env.config], capture_output=True, text=True,
                           timeout=10)
        self.assertNotEqual(p.returncode, 0)
        self.assertIn("in use", p.stderr)
        self.assertEqual(self.env.ctl("ping")[0], 0)


def main():
    global WIFIMGRD, WIFICTL
    ap = argparse.ArgumentParser()
    ap.add_argument("--wifimgrd", required=True)
    ap.add_argument("--wifictl", required=True)
    args, rest = ap.parse_known_args()
    WIFIMGRD = os.path.abspath(args.wifimgrd)
    WIFICTL = os.path.abspath(args.wifictl)
    unittest.main(argv=[sys.argv[0]] + rest, verbosity=2)


if __name__ == "__main__":
    main()
