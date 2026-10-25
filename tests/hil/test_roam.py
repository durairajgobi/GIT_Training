# SPDX-License-Identifier: MIT
"""Head unit roaming between depot APs (802.11r) while attenuation sweeps."""
from __future__ import annotations

import pytest

pytestmark = pytest.mark.roam

SSID = "DEPOT-OTA"
ROAM_BUDGET_MS = 150


@pytest.fixture
def two_aps(testbed):
    if len(testbed.aps) < 2:
        pytest.skip("testbed has fewer than two APs")
    a, b = testbed.aps[:2]
    for ap in (a, b):
        ap.configure(SSID, testbed.passphrase, "wpa3", testbed.roam_channel)
    a.set_attenuation(0)
    b.set_attenuation(40)
    r = testbed.head_unit.connect(SSID, testbed.passphrase, "wpa3")
    assert r.ok, r.reason
    assert r.bssid == a.bssid
    return a, b


@pytest.mark.parametrize("step_db", [2, 5, 10])
def test_roam_on_attenuation_sweep(testbed, two_aps, step_db):
    a, b = two_aps
    testbed.sniffer.start(testbed.roam_channel, f"roam-{step_db}db")
    att_a, att_b = 0, 40
    while testbed.head_unit.status().get("bssid") != b.bssid and att_a < 60:
        att_a, att_b = att_a + step_db, max(0, att_b - step_db)
        a.set_attenuation(att_a)
        b.set_attenuation(att_b)
    testbed.sniffer.stop()

    assert testbed.head_unit.status().get("bssid") == b.bssid, "never roamed"
    t = testbed.sniffer.roam_time_ms(testbed.head_unit.mac)
    assert t is not None, "no reassociation seen (hard disconnect instead of roam)"
    assert t < ROAM_BUDGET_MS


def test_no_ping_pong(testbed, two_aps):
    """Small RSSI wobble around the threshold must not cause roams."""
    a, _ = two_aps
    testbed.sniffer.start(testbed.roam_channel, "pingpong")
    for att in (8, 12, 8, 12, 8):
        a.set_attenuation(att)
    testbed.sniffer.stop()
    assert testbed.head_unit.status().get("bssid") == a.bssid
    assert testbed.sniffer.roam_time_ms(testbed.head_unit.mac) is None
