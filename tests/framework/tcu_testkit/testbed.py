# SPDX-License-Identifier: MIT
"""Testbed description -> device objects.

A testbed is a JSON file describing the bench (see tests/testbeds/). The
special name ``sim`` builds the simulated testbed.
"""
from __future__ import annotations

import json
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

from .devices import HeadUnit, ProgrammableAP, Sniffer, TcuDut
from .transport import SSHTransport

REPO = Path(__file__).resolve().parents[3]


@dataclass
class Testbed:
    name: str
    dut: Any
    head_unit: Any
    legacy_client: Any       # WPA2-only phone/tablet for transition-mode cases
    aps: list[Any]
    sniffer: Any
    hotspot_channels: list[int] = field(default_factory=lambda: [6, 36])
    roam_channel: int = 36
    bt_phone: str = ""
    passphrase: str = "lab-passphrase-1"
    internet_probe: str = "http://connectivitycheck.gstatic.com/generate_204"
    download_url: str = "http://192.168.200.1/100MB.bin"
    simulated: bool = False


def _ssh(spec: dict) -> SSHTransport:
    return SSHTransport(spec["host"], spec.get("user", "root"), spec.get("port", 22),
                        spec.get("key"), spec.get("jump"))


def load(name_or_path: str) -> Testbed:
    if name_or_path == "sim":
        return build_sim()
    cfg = json.loads(Path(name_or_path).read_text())
    dut = TcuDut(_ssh(cfg["dut"]))
    return Testbed(
        name=cfg["name"],
        dut=dut,
        head_unit=HeadUnit(_ssh(cfg["head_unit"]), cfg["head_unit"].get("iface", "wlan0")),
        legacy_client=HeadUnit(_ssh(cfg["legacy_client"]), cfg["legacy_client"].get("iface", "wlan0"),
                               supports_sae=False),
        aps=[ProgrammableAP(_ssh(a), a["bssid"], a.get("conf", "/etc/hostapd/hostapd.conf"),
                            a.get("attenuator_cmd")) for a in cfg["aps"]],
        sniffer=Sniffer(_ssh(cfg["sniffer"]), cfg["sniffer"].get("iface", "mon0")),
        hotspot_channels=cfg.get("hotspot_channels", [6, 36]),
        roam_channel=cfg.get("roam_channel", 36),
        bt_phone=cfg.get("bt_phone", ""),
        passphrase=cfg.get("passphrase", "lab-passphrase-1"),
        internet_probe=cfg.get("internet_probe", Testbed.internet_probe),
        download_url=cfg.get("download_url", Testbed.download_url),
    )


def build_sim() -> Testbed:
    from .sim import SimAP, SimHeadUnit, SimSniffer, SimTcu, SimWorld

    world = SimWorld()
    policy = (REPO / "config" / "hotspot-policy.json").read_text()
    tcu = SimTcu(world, policy)
    tcu.bt_links.add("aa:bb:cc:00:00:01")
    return Testbed(
        name="sim",
        dut=tcu,
        # 02:..:02 has a 2 GB quota in the sample policy, 02:..:01 is exempt
        head_unit=SimHeadUnit(world, tcu, "02:00:00:00:00:02"),
        legacy_client=SimHeadUnit(world, tcu, "02:00:00:00:00:10", supports_sae=False),
        aps=[SimAP(world, "02:a0:00:00:00:01"), SimAP(world, "02:a0:00:00:00:02")],
        sniffer=SimSniffer(world),
        hotspot_channels=[1, 6, 11, 36, 149],
        bt_phone="aa:bb:cc:00:00:01",
        passphrase="sim-passphrase",
        simulated=True,
    )
