#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Per-passenger data policy for the in-truck hotspot.

Renders an nftables ruleset from the policy file and the DHCP lease table:

* unauthorised clients only reach DNS/DHCP and the captive portal
  (HTTP is DNAT'ed to the portal);
* each authorised passenger has a named quota (shared by uplink and
  downlink) and a named rate limit;
* the driver/fleet SSID class is exempt from quotas.

The ruleset is applied atomically with ``nft -f`` so a policy reload
never leaves the hotspot open.

    hotspot_policy.py render  --policy /etc/tcu/hotspot-policy.json
    hotspot_policy.py apply   --policy ... [--leases /var/lib/misc/dnsmasq.leases]
    hotspot_policy.py authorize aa:bb:cc:dd:ee:ff
"""
from __future__ import annotations

import argparse
import ipaddress
import json
import re
import subprocess
import sys
from dataclasses import dataclass, field
from typing import Callable, Sequence

TABLE = "tcu_hotspot"
MAC_RE = re.compile(r"^[0-9a-f]{2}(:[0-9a-f]{2}){5}$")

Runner = Callable[[Sequence[str], str | None], None]


def run_nft(args: Sequence[str], stdin: str | None = None) -> None:
    subprocess.run(["nft", *args], input=stdin, text=True, check=True)


def norm_mac(mac: str) -> str:
    m = mac.strip().lower().replace("-", ":")
    if not MAC_RE.match(m):
        raise ValueError(f"invalid MAC address: {mac!r}")
    return m


@dataclass(frozen=True)
class Passenger:
    mac: str
    quota_mb: int = 1024
    rate_kbps: int = 4096
    exempt: bool = False  # driver devices / fleet tablet

    def key(self) -> str:
        return self.mac.replace(":", "")


@dataclass
class HotspotConfig:
    ap_if: str = "ap0"
    wan_if: str = "wwan0"
    portal_ip: str = "192.168.50.1"
    portal_port: int = 8080
    subnet: str = "192.168.50.0/24"
    default_quota_mb: int = 1024
    default_rate_kbps: int = 4096
    passengers: list[Passenger] = field(default_factory=list)

    @classmethod
    def from_json(cls, text: str) -> "HotspotConfig":
        raw = json.loads(text)
        cfg = cls(**{k: v for k, v in raw.items() if k != "passengers"})
        ipaddress.ip_network(cfg.subnet)  # validate early
        for p in raw.get("passengers", []):
            cfg.passengers.append(
                Passenger(
                    mac=norm_mac(p["mac"]),
                    quota_mb=int(p.get("quota_mb", cfg.default_quota_mb)),
                    rate_kbps=int(p.get("rate_kbps", cfg.default_rate_kbps)),
                    exempt=bool(p.get("exempt", False)),
                )
            )
        return cfg


def parse_leases(text: str) -> dict[str, str]:
    """dnsmasq lease file -> {mac: ipv4}. Format: expiry mac ip host id."""
    leases: dict[str, str] = {}
    for line in text.splitlines():
        parts = line.split()
        if len(parts) >= 3:
            try:
                leases[norm_mac(parts[1])] = str(ipaddress.IPv4Address(parts[2]))
            except ValueError:
                continue
    return leases


@dataclass
class LiveState:
    """What must survive a re-render: portal sign-ins and quota usage."""

    authorized: set[str] = field(default_factory=set)
    quota_used: dict[str, int] = field(default_factory=dict)  # name -> bytes


def parse_live_state(nft_json: str) -> LiveState:
    """Parse ``nft -j list table inet tcu_hotspot``."""
    state = LiveState()
    for obj in json.loads(nft_json).get("nftables", []):
        if "set" in obj and obj["set"].get("name") == "authorized":
            for e in obj["set"].get("elem", []):
                if isinstance(e, str):
                    state.authorized.add(norm_mac(e))
        elif "quota" in obj:
            q = obj["quota"]
            state.quota_used[q["name"]] = int(q.get("used", 0))
    return state


def read_live_state() -> LiveState:
    r = subprocess.run(["nft", "-j", "list", "table", "inet", TABLE],
                       capture_output=True, text=True)
    return parse_live_state(r.stdout) if r.returncode == 0 and r.stdout else LiveState()


def render(cfg: HotspotConfig, leases: dict[str, str], live: LiveState | None = None) -> str:
    live = live or LiveState()
    auth = sorted(live.authorized | {p.mac for p in cfg.passengers if p.exempt})
    out: list[str] = [
        f"table inet {TABLE}",
        f"delete table inet {TABLE}",
        f"table inet {TABLE} {{",
        "\tset authorized {",
        "\t\ttype ether_addr",
    ]
    if auth:
        out.append("\t\telements = { " + ", ".join(auth) + " }")
    out.append("\t}")

    metered = [p for p in cfg.passengers if not p.exempt]
    for p in metered:
        used = live.quota_used.get(f"q_{p.key()}", 0)
        out.append(f"\tquota q_{p.key()} {{ over {p.quota_mb} mbytes used {used} bytes }}")
        # nft limits are in bytes/second
        out.append(f"\tlimit l_{p.key()} {{ rate over {p.rate_kbps * 1000 // 8} bytes/second }}")

    out += [
        "\tchain prerouting {",
        "\t\ttype nat hook prerouting priority dstnat; policy accept;",
        f'\t\tiifname "{cfg.ap_if}" ether saddr != @authorized tcp dport 80 '
        f"dnat ip to {cfg.portal_ip}:{cfg.portal_port}",
        "\t}",
        "\tchain postrouting {",
        "\t\ttype nat hook postrouting priority srcnat; policy accept;",
        f'\t\toifname "{cfg.wan_if}" ip saddr {cfg.subnet} masquerade',
        "\t}",
        "\tchain input {",
        "\t\ttype filter hook input priority filter; policy accept;",
        f'\t\tiifname "{cfg.ap_if}" udp dport {{ 53, 67 }} accept',
        f'\t\tiifname "{cfg.ap_if}" tcp dport {{ 53, {cfg.portal_port} }} accept',
        # Passengers must never reach the TCU's own services (SSH etc.).
        f'\t\tiifname "{cfg.ap_if}" drop',
        "\t}",
        "\tchain forward {",
        "\t\ttype filter hook forward priority filter; policy drop;",
        "\t\tct state invalid drop",
        f'\t\tiifname "{cfg.ap_if}" ether saddr != @authorized drop',
        # No passenger-to-passenger traffic (AP isolation is also on in hostapd).
        f'\t\tiifname "{cfg.ap_if}" oifname "{cfg.ap_if}" drop',
    ]
    for p in metered:
        ip = leases.get(p.mac)
        if not ip:
            continue  # not associated yet; re-rendered on lease change
        k = p.key()
        out += [
            f'\t\tiifname "{cfg.ap_if}" ip saddr {ip} quota name "q_{k}" drop',
            f'\t\toifname "{cfg.ap_if}" ip daddr {ip} quota name "q_{k}" drop',
            f'\t\tiifname "{cfg.ap_if}" ip saddr {ip} limit name "l_{k}" drop',
            f'\t\toifname "{cfg.ap_if}" ip daddr {ip} limit name "l_{k}" drop',
        ]
    out += [
        f'\t\tiifname "{cfg.ap_if}" oifname "{cfg.wan_if}" accept',
        f'\t\tiifname "{cfg.wan_if}" oifname "{cfg.ap_if}" ct state established,related accept',
        "\t}",
        "}",
    ]
    return "\n".join(out) + "\n"


def authorize(mac: str, runner: Runner = run_nft) -> None:
    runner(["add", "element", "inet", TABLE, "authorized", "{", norm_mac(mac), "}"], None)


def revoke(mac: str, runner: Runner = run_nft) -> None:
    runner(["delete", "element", "inet", TABLE, "authorized", "{", norm_mac(mac), "}"], None)


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    for name in ("render", "apply"):
        s = sub.add_parser(name)
        s.add_argument("--policy", default="/etc/tcu/hotspot-policy.json")
        s.add_argument("--leases", default="/var/lib/misc/dnsmasq.leases")
    for name in ("authorize", "revoke"):
        sub.add_parser(name).add_argument("mac")
    args = ap.parse_args(argv)

    if args.cmd in ("authorize", "revoke"):
        (authorize if args.cmd == "authorize" else revoke)(args.mac)
        return 0

    with open(args.policy, encoding="utf-8") as f:
        cfg = HotspotConfig.from_json(f.read())
    try:
        with open(args.leases, encoding="utf-8") as f:
            leases = parse_leases(f.read())
    except FileNotFoundError:
        leases = {}
    if args.cmd == "render":
        sys.stdout.write(render(cfg, leases))
    else:
        run_nft(["-f", "-"], render(cfg, leases, read_live_state()))
    return 0


if __name__ == "__main__":
    sys.exit(main())
