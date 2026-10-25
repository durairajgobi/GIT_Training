# SPDX-License-Identifier: MIT
import json
import shutil
import subprocess

import pytest

import hotspot_policy as hp

POLICY = json.dumps({
    "passengers": [
        {"mac": "02:00:00:00:00:01", "exempt": True},
        {"mac": "02-00-00-00-00-02", "quota_mb": 500, "rate_kbps": 2000},
        {"mac": "02:00:00:00:00:03"},
    ]
})


@pytest.fixture
def cfg():
    return hp.HotspotConfig.from_json(POLICY)


def test_defaults_and_mac_normalisation(cfg):
    p2, p3 = cfg.passengers[1], cfg.passengers[2]
    assert p2.mac == "02:00:00:00:00:02"
    assert (p3.quota_mb, p3.rate_kbps) == (1024, 4096)


@pytest.mark.parametrize("bad", ["zz:00:00:00:00:00", "02:00:00:00:00", ""])
def test_invalid_mac_rejected(bad):
    with pytest.raises(ValueError):
        hp.norm_mac(bad)


def test_render_exempt_preauthorised_and_unmetered(cfg):
    rs = hp.render(cfg, {})
    assert "elements = { 02:00:00:00:00:01 }" in rs
    assert "q_020000000001" not in rs


def test_render_quota_and_limit_only_for_leased_clients(cfg):
    rs = hp.render(cfg, {"02:00:00:00:00:02": "192.168.50.12"})
    assert "quota q_020000000002 { over 500 mbytes used 0 bytes }" in rs
    assert "limit l_020000000002 { rate over 250000 bytes/second }" in rs
    assert 'ip saddr 192.168.50.12 quota name "q_020000000002" drop' in rs
    assert 'ip daddr 192.168.50.12 quota name "q_020000000002" drop' in rs
    # declared but not yet leased: no per-IP rules
    assert "q_020000000003" in rs and "ip saddr 192.168.50.1 " not in rs


def test_forward_policy_is_default_drop(cfg):
    rs = hp.render(cfg, {})
    assert "type filter hook forward priority filter; policy drop;" in rs
    assert 'iifname "ap0" ether saddr != @authorized drop' in rs
    assert 'iifname "ap0" drop' in rs  # input: no TCU services for passengers


def test_live_state_carried_across_reload(cfg):
    live = hp.parse_live_state(json.dumps({"nftables": [
        {"metainfo": {"version": "1.0.9"}},
        {"set": {"name": "authorized", "type": "ether_addr", "elem": ["AA:BB:CC:DD:EE:FF"]}},
        {"quota": {"name": "q_020000000002", "bytes": 524288000, "used": 1234, "inv": True}},
    ]}))
    rs = hp.render(cfg, {}, live)
    assert "aa:bb:cc:dd:ee:ff" in rs
    assert "over 500 mbytes used 1234 bytes" in rs


def test_parse_leases_skips_garbage():
    leases = hp.parse_leases(
        "1700000000 02:00:00:00:00:02 192.168.50.12 phone *\n"
        "garbage line\n"
        "1700000000 zz:zz 192.168.50.13 x *\n"
        "1700000000 02:00:00:00:00:03 not-an-ip x *\n")
    assert leases == {"02:00:00:00:00:02": "192.168.50.12"}


def test_authorize_uses_runner():
    calls = []
    hp.authorize("02-00-00-00-00-09", runner=lambda a, s: calls.append(a))
    assert calls == [["add", "element", "inet", "tcu_hotspot", "authorized", "{", "02:00:00:00:00:09", "}"]]


@pytest.mark.skipif(not shutil.which("nft"), reason="nft not installed")
def test_ruleset_passes_nft_syntax_check(cfg):
    rs = hp.render(cfg, {"02:00:00:00:00:02": "192.168.50.12"},
                   hp.LiveState({"aa:bb:cc:dd:ee:ff"}, {"q_020000000002": 99}))
    r = subprocess.run(["nft", "-c", "-f", "-"], input=rs, text=True, capture_output=True)
    if r.returncode != 0 and "Operation not permitted" in r.stderr:
        pytest.skip("nft -c needs netlink access here")
    assert r.returncode == 0, r.stderr
