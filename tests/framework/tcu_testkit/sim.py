# SPDX-License-Identifier: MIT
"""Simulated testbed with the same device API as ``devices.py``.

Used to self-test the framework and the test cases in CI without RF
hardware. The hotspot side runs the production captive-portal and policy
code (``src/hotspot``); the radio side is a deliberately simple model:

* RSSI = tx_power - path_loss - attenuation
* a client roams when its RSSI < -70 dBm and another BSS is 8 dB better
* SAE needs client support; WPA3-only rejects WPA2-only clients
"""
from __future__ import annotations

import random
from dataclasses import dataclass, field

from .devices import ConnectResult

try:
    import captive_portal
    import hotspot_policy
except ImportError:  # pragma: no cover - conftest puts src/hotspot on sys.path
    raise


@dataclass
class SimBss:
    bssid: str
    ssid: str
    passphrase: str
    security: str
    channel: int
    tx_dbm: float = 20.0
    path_loss_db: float = 60.0
    attenuation_db: float = 0.0

    def rssi(self) -> float:
        return self.tx_dbm - self.path_loss_db - self.attenuation_db


@dataclass
class SimWorld:
    seed: int = 1234
    bss: dict[str, SimBss] = field(default_factory=dict)
    stations: list["SimHeadUnit"] = field(default_factory=list)
    roam_log: dict[str, list[float]] = field(default_factory=dict)

    def __post_init__(self):
        self.rng = random.Random(self.seed)

    def candidates(self, ssid: str) -> list[SimBss]:
        return sorted((b for b in self.bss.values() if b.ssid == ssid and b.rssi() > -88),
                      key=lambda b: b.rssi(), reverse=True)

    def radio_changed(self) -> None:
        for sta in self.stations:
            sta.evaluate_roam()


class SimTcu:
    BSSID = "02:7c:00:00:00:01"

    def __init__(self, world: SimWorld, policy_json: str):
        self.world = world
        self.policy = hotspot_policy.HotspotConfig.from_json(policy_json)
        self.portal_url = f"http://{self.policy.portal_ip}:{self.policy.portal_port}/terms"
        self.leases: dict[str, str] = {}  # mac -> ip
        self.usage_mb: dict[str, float] = {}
        self.authorized: set[str] = set()
        self.portal = captive_portal.Portal(
            self.portal_url, self.policy.default_quota_mb,
            resolve_mac=lambda ip: next((m for m, i in self.leases.items() if i == ip), None),
            authorize=self.authorized.add,
        )
        self.bt_links: set[str] = set()
        self.services = {"ecalld", "ltecm", "btmgr", "tcu-pmd", "tcu-hotspot"}
        self.configure_hotspot("TRUCK-WIFI", "sim-passphrase", "wpa3-transition", 36)

    # --- hotspot ---
    def configure_hotspot(self, ssid: str, passphrase: str, security: str, channel: int) -> None:
        self.world.bss[self.BSSID] = SimBss(self.BSSID, ssid, passphrase, security, channel,
                                            path_loss_db=45.0)
        for sta in self.world.stations:
            if sta.bssid == self.BSSID:
                sta.drop()
        self.leases.clear()

    def hotspot_stations(self) -> dict[str, dict[str, str]]:
        return {s.mac: {"flags": "[AUTH][ASSOC][AUTHORIZED]", "key_mgmt": s.key_mgmt}
                for s in self.world.stations if s.bssid == self.BSSID}

    def revoke_all_portal_sessions(self) -> None:
        self.authorized.clear()
        self.portal.authorized.clear()

    def dhcp(self, mac: str) -> str:
        if mac not in self.leases:
            self.leases[mac] = f"192.168.50.{10 + len(self.leases)}"
        return self.leases[mac]

    def quota_mb(self, mac: str) -> float:
        for p in self.policy.passengers:
            if p.mac == mac:
                return float("inf") if p.exempt else p.quota_mb
        return self.policy.default_quota_mb

    def is_exempt(self, mac: str) -> bool:
        return any(p.mac == mac and p.exempt for p in self.policy.passengers)

    # --- misc ---
    def bt_connected(self, addr: str) -> bool:
        return addr.lower() in self.bt_links

    def service_active(self, name: str) -> bool:
        return name.removesuffix(".service") in self.services

    def collect_logs(self, since: str = "-15min") -> str:
        return "sim: no journal\n"


class SimHeadUnit:
    def __init__(self, world: SimWorld, tcu: SimTcu, mac: str, supports_sae: bool = True):
        self.world = world
        self.tcu = tcu
        self.mac = mac.lower()
        self.supports_sae = supports_sae
        self.bssid = ""
        self.ssid = ""
        self.key_mgmt = ""
        self.ip = ""
        world.stations.append(self)

    def status(self) -> dict[str, str]:
        if not self.bssid:
            return {"wpa_state": "DISCONNECTED"}
        return {"wpa_state": "COMPLETED", "bssid": self.bssid, "ssid": self.ssid,
                "key_mgmt": self.key_mgmt, "ip_address": self.ip}

    def _key_mgmt_for(self, bss: SimBss) -> str | None:
        if bss.security == "wpa2":
            return "WPA2-PSK"
        if bss.security == "wpa3":
            return "SAE" if self.supports_sae else None
        return "SAE" if self.supports_sae else "WPA2-PSK"

    def connect(self, ssid: str, passphrase: str, security: str, timeout: float = 20) -> ConnectResult:
        self.drop()
        cands = self.world.candidates(ssid)
        if not cands:
            return ConnectResult(False, 0, reason="network not found")
        bss = cands[0]
        km = self._key_mgmt_for(bss)
        if km is None:
            return ConnectResult(False, 0, reason="no common AKM (client lacks SAE)")
        if passphrase != bss.passphrase:
            return ConnectResult(False, 0, reason="4-way handshake failed")
        self.bssid, self.ssid, self.key_mgmt = bss.bssid, ssid, km
        if bss.bssid == SimTcu.BSSID:
            self.ip = self.tcu.dhcp(self.mac)
        # SAE commit/confirm costs more than PSK; DHCP adds the rest
        base = 450 if km == "SAE" else 300
        return ConnectResult(True, base + self.world.rng.uniform(0, 400), bss.bssid, km)

    def drop(self) -> None:
        self.bssid = self.ssid = self.key_mgmt = self.ip = ""

    def disconnect(self) -> None:
        self.drop()

    def evaluate_roam(self) -> None:
        if not self.bssid or self.bssid not in self.world.bss:
            return
        cur = self.world.bss[self.bssid]
        best = self.world.candidates(self.ssid)
        if cur.rssi() <= -88:
            self.drop()  # link lost before a roam could happen
            if best:
                self.connect(self.ssid, best[0].passphrase, best[0].security)
            return
        if best and best[0].bssid != cur.bssid and cur.rssi() < -70 and best[0].rssi() - cur.rssi() >= 8:
            self.bssid = best[0].bssid
            self.world.roam_log.setdefault(self.mac, []).append(self.world.rng.uniform(25, 90))

    # --- traffic through the TCU hotspot ---
    def _authorized(self) -> bool:
        return self.mac in self.tcu.authorized or self.tcu.is_exempt(self.mac)

    def http_probe(self, url: str) -> tuple[int, str]:
        if self.bssid != SimTcu.BSSID:
            return 0, ""
        if not self._authorized():
            return 302, self.tcu.portal_url
        if self.tcu.usage_mb.get(self.mac, 0) >= self.tcu.quota_mb(self.mac):
            return 0, ""  # quota exhausted: dropped in forward chain
        return 204, ""

    def accept_portal(self, portal_url: str) -> bool:
        return self.bssid == SimTcu.BSSID and self.tcu.portal.accept(self.ip)

    def download_mb(self, url: str, max_mb: int) -> float:
        if self.http_probe(url)[0] != 204:
            return 0.0
        used = self.tcu.usage_mb.get(self.mac, 0.0)
        got = max(0.0, min(float(max_mb), self.tcu.quota_mb(self.mac) - used))
        self.tcu.usage_mb[self.mac] = used + got
        return got


class SimAP:
    def __init__(self, world: SimWorld, bssid: str):
        self.world = world
        self.bssid = bssid.lower()

    def configure(self, ssid: str, passphrase: str, security: str, channel: int) -> None:
        old = self.world.bss.get(self.bssid)
        self.world.bss[self.bssid] = SimBss(self.bssid, ssid, passphrase, security, channel,
                                            attenuation_db=old.attenuation_db if old else 0.0)

    def set_attenuation(self, db: float) -> None:
        self.world.bss[self.bssid].attenuation_db = db
        self.world.radio_changed()


class SimSniffer:
    def __init__(self, world: SimWorld):
        self.world = world
        self._mark: dict[str, int] = {}

    def start(self, channel: int, name: str) -> None:
        self._mark = {m: len(v) for m, v in self.world.roam_log.items()}

    def stop(self) -> str:
        return "sim.pcap"

    def roam_time_ms(self, sta: str) -> float | None:
        log = self.world.roam_log.get(sta.lower(), [])
        return log[-1] if len(log) > self._mark.get(sta.lower(), 0) else None

    def count(self, display_filter: str) -> int:
        return 0
