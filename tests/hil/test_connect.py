# SPDX-License-Identifier: MIT
"""Hotspot association matrix: security mode x channel x client type."""
from __future__ import annotations

import pytest

from tcu_testkit import SECURITY_MODES, band_of

pytestmark = pytest.mark.connect

CONNECT_BUDGET_MS = {"2g": 3000, "5g": 3000}
SSID = "TRUCK-WIFI-T"


def pytest_generate_tests(metafunc):
    # The channel list comes from the bench, so the matrix grows with it.
    if "channel" in metafunc.fixturenames:
        from tcu_testkit import load

        tb = load(metafunc.config.getoption("--testbed"))
        metafunc.parametrize("channel", tb.hotspot_channels, ids=lambda c: f"ch{c}")


@pytest.mark.parametrize("security", SECURITY_MODES)
def test_head_unit_connects(testbed, security, channel):
    testbed.dut.configure_hotspot(SSID, testbed.passphrase, security, channel)
    r = testbed.head_unit.connect(SSID, testbed.passphrase, security)
    assert r.ok, r.reason
    assert r.time_ms < CONNECT_BUDGET_MS[band_of(channel)]
    expected = "SAE" if security != "wpa2" else "WPA2-PSK"
    assert r.key_mgmt == expected
    assert testbed.head_unit.mac in testbed.dut.hotspot_stations()


@pytest.mark.parametrize("security,should_connect", [
    ("wpa2", True), ("wpa3-transition", True), ("wpa3", False),
])
def test_legacy_client(testbed, security, should_connect):
    """WPA3 migration: transition mode must keep WPA2-only phones working."""
    testbed.dut.configure_hotspot(SSID, testbed.passphrase, security, testbed.hotspot_channels[0])
    r = testbed.legacy_client.connect(SSID, testbed.passphrase, security)
    assert r.ok is should_connect, r.reason
    if r.ok:
        assert r.key_mgmt == "WPA2-PSK"


@pytest.mark.parametrize("security", SECURITY_MODES)
def test_wrong_passphrase_rejected(testbed, security):
    testbed.dut.configure_hotspot(SSID, testbed.passphrase, security, testbed.hotspot_channels[-1])
    r = testbed.head_unit.connect(SSID, testbed.passphrase + "x", security)
    assert not r.ok
    assert testbed.head_unit.mac not in testbed.dut.hotspot_stations()
