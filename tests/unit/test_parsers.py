# SPDX-License-Identifier: MIT
from tcu_testkit.parsers import (parse_bluetoothctl_info, parse_curl_probe,
                                 parse_hostapd_all_sta, parse_kv)


def test_hostapd_all_sta():
    out = """02:00:00:00:00:02
flags=[AUTH][ASSOC][AUTHORIZED][WMM][MFP]
AKMSuiteSelector=000fac-8
signal=-41
02:00:00:00:00:10
flags=[AUTH][ASSOC][AUTHORIZED]
"""
    sta = parse_hostapd_all_sta(out)
    assert set(sta) == {"02:00:00:00:00:02", "02:00:00:00:00:10"}
    assert sta["02:00:00:00:00:02"]["AKMSuiteSelector"] == "000fac-8"


def test_wpa_cli_status():
    st = parse_kv("bssid=02:a0:00:00:00:01\nssid=DEPOT\nwpa_state=COMPLETED\nkey_mgmt=SAE\n")
    assert st["wpa_state"] == "COMPLETED" and st["key_mgmt"] == "SAE"


def test_bluetoothctl_info():
    info = parse_bluetoothctl_info("Device AA:BB:CC:00:00:01 (public)\n\tName: Phone\n"
                                   "\tPaired: yes\n\tConnected: no\n\tUUID: Handsfree Audio Gateway (0000111f-...)\n")
    assert info["Connected"] == "no" and info["Paired"] == "yes"


def test_curl_probe():
    assert parse_curl_probe("302 http://192.168.50.1:8080/terms") == (302, "http://192.168.50.1:8080/terms")
    assert parse_curl_probe("204 ") == (204, "")
    assert parse_curl_probe("") == (0, "")
