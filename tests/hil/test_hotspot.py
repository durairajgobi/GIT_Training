# SPDX-License-Identifier: MIT
"""Captive portal onboarding and per-passenger data policy."""
from __future__ import annotations

import pytest

pytestmark = pytest.mark.hotspot

SSID = "TRUCK-WIFI-H"


@pytest.fixture
def on_hotspot(testbed):
    testbed.dut.configure_hotspot(SSID, testbed.passphrase, "wpa3-transition",
                                  testbed.hotspot_channels[-1])
    testbed.dut.revoke_all_portal_sessions()
    r = testbed.head_unit.connect(SSID, testbed.passphrase, "wpa3-transition")
    assert r.ok, r.reason
    return testbed.head_unit


def test_unauthorised_client_is_captive(testbed, on_hotspot):
    code, location = on_hotspot.http_probe(testbed.internet_probe)
    assert code == 302
    assert location == testbed.dut.portal_url


def test_accept_terms_opens_internet(testbed, on_hotspot):
    assert on_hotspot.accept_portal(testbed.dut.portal_url)
    code, _ = on_hotspot.http_probe(testbed.internet_probe)
    assert code == 204


def test_authorisation_survives_reconnect(testbed, on_hotspot):
    assert on_hotspot.accept_portal(testbed.dut.portal_url)
    on_hotspot.disconnect()
    r = on_hotspot.connect(SSID, testbed.passphrase, "wpa3-transition")
    assert r.ok
    assert on_hotspot.http_probe(testbed.internet_probe)[0] == 204


@pytest.mark.skipif("not config.getoption('--testbed') == 'sim'",
                    reason="full quota burn takes ~20 min on LTE; covered by the weekly soak")
def test_quota_enforced(testbed, on_hotspot):
    assert on_hotspot.accept_portal(testbed.dut.portal_url)
    quota = testbed.dut.quota_mb(on_hotspot.mac)
    got = on_hotspot.download_mb(testbed.download_url, int(quota) + 100)
    assert got == pytest.approx(quota, abs=1)
    assert on_hotspot.http_probe(testbed.internet_probe)[0] != 204

