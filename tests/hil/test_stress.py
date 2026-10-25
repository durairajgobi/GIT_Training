# SPDX-License-Identifier: MIT
"""Repetition and churn: the failures field units see after weeks, not minutes."""
from __future__ import annotations

import pytest

pytestmark = pytest.mark.stress

SSID = "TRUCK-WIFI-S"


def test_connect_disconnect_cycles(testbed, stress_iterations):
    testbed.dut.configure_hotspot(SSID, testbed.passphrase, "wpa3-transition",
                                  testbed.hotspot_channels[-1])
    failures, times = [], []
    for i in range(stress_iterations):
        r = testbed.head_unit.connect(SSID, testbed.passphrase, "wpa3-transition")
        if not r.ok:
            failures.append((i, r.reason))
        else:
            times.append(r.time_ms)
        testbed.head_unit.disconnect()
    assert not failures, f"{len(failures)}/{stress_iterations} failed: {failures[:5]}"
    times.sort()
    assert times[int(len(times) * 0.95) - 1] < 3000, "p95 connect time over budget"


def test_hotspot_reconfigure_churn(testbed, stress_iterations):
    """Security changes (WPA3 migration pushes) must never wedge hostapd."""
    modes = ["wpa2", "wpa3-transition", "wpa3"]
    for i in range(stress_iterations):
        mode = modes[i % 3]
        testbed.dut.configure_hotspot(SSID, testbed.passphrase, mode, testbed.hotspot_channels[0])
        assert testbed.dut.service_active("tcu-hotspot.service")
    r = testbed.head_unit.connect(SSID, testbed.passphrase, modes[(stress_iterations - 1) % 3])
    assert r.ok, r.reason


def test_two_clients_concurrently(testbed, stress_iterations):
    testbed.dut.configure_hotspot(SSID, testbed.passphrase, "wpa3-transition",
                                  testbed.hotspot_channels[0])
    for _ in range(stress_iterations):
        a = testbed.head_unit.connect(SSID, testbed.passphrase, "wpa3-transition")
        b = testbed.legacy_client.connect(SSID, testbed.passphrase, "wpa3-transition")
        assert a.ok and b.ok, (a.reason, b.reason)
        stations = testbed.dut.hotspot_stations()
        assert {testbed.head_unit.mac, testbed.legacy_client.mac} <= stations.keys()
        testbed.legacy_client.disconnect()


@pytest.mark.bt
def test_platform_services_stay_up(testbed):
    """After the whole stress run nothing may have crashed."""
    for svc in ("ecalld", "ltecm", "btmgr", "tcu-pmd", "tcu-hotspot"):
        assert testbed.dut.service_active(f"{svc}.service"), svc
    if testbed.bt_phone:
        assert testbed.dut.bt_connected(testbed.bt_phone)
