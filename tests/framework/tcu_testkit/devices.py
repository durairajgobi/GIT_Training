# SPDX-License-Identifier: MIT
"""Lab device drivers: TCU (DUT), secondary head unit, programmable APs, sniffer.

Every device talks over a Transport, so the same driver works on a bench,
in an RF shield box or through a jump host. The simulated testbed in
``sim.py`` implements the same methods for framework self-tests.
"""
from __future__ import annotations

import re
import time
from dataclasses import dataclass
from urllib.parse import urlparse

from .parsers import parse_bluetoothctl_info, parse_curl_probe, parse_hostapd_all_sta, parse_kv
from .transport import Transport, q

SECURITY_MODES = ("wpa2", "wpa3", "wpa3-transition")

# hostapd / wpa_supplicant settings per security mode
HOSTAPD_SECURITY = {
    "wpa2": {"wpa_key_mgmt": "WPA-PSK", "ieee80211w": "0"},
    "wpa3": {"wpa_key_mgmt": "SAE", "ieee80211w": "2"},
    "wpa3-transition": {"wpa_key_mgmt": "SAE WPA-PSK", "ieee80211w": "1"},
}
SUPPLICANT_SECURITY = {
    "wpa2": {"key_mgmt": "WPA-PSK", "ieee80211w": "0"},
    "wpa3": {"key_mgmt": "SAE", "ieee80211w": "2"},
    "wpa3-transition": {"key_mgmt": "SAE WPA-PSK", "ieee80211w": "1"},
}


@dataclass
class ConnectResult:
    ok: bool
    time_ms: float
    bssid: str = ""
    key_mgmt: str = ""
    reason: str = ""


def band_of(channel: int) -> str:
    return "2g" if channel <= 14 else "5g"


def _hostapd_overrides(ssid: str, passphrase: str, security: str, channel: int) -> dict[str, str]:
    conf = {
        "ssid": ssid,
        "wpa_passphrase": passphrase,
        "channel": str(channel),
        "hw_mode": "g" if channel <= 14 else "a",
        **HOSTAPD_SECURITY[security],
    }
    if security != "wpa2":
        conf["sae_require_mfp"] = "1"
    return conf


def _apply_hostapd_conf(t: Transport, path: str, conf: dict[str, str]) -> None:
    # Rewrite keys in place (and append missing ones) in a single remote shell.
    script = []
    for k, v in conf.items():
        line = f"{k}={v}"
        script.append(
            f"grep -q '^{k}=' {path} && sed -i 's|^{k}=.*|{line}|' {path} "
            f"|| echo {q(line)} >> {path}"
        )
    t.run(" ; ".join(script)).check()


class TcuDut:
    """The telematics unit, running the -dev image."""

    def __init__(self, t: Transport, ap_if: str = "ap0", portal_url: str = "http://192.168.50.1:8080/terms"):
        self.t = t
        self.ap_if = ap_if
        self.portal_url = portal_url

    def configure_hotspot(self, ssid: str, passphrase: str, security: str, channel: int) -> None:
        _apply_hostapd_conf(self.t, "/etc/tcu/hostapd-ap0.conf",
                            _hostapd_overrides(ssid, passphrase, security, channel))
        self.t.run("systemctl restart tcu-hotspot.service", timeout=60).check()

    def hotspot_stations(self) -> dict[str, dict[str, str]]:
        return parse_hostapd_all_sta(self.t.run(f"hostapd_cli -i {self.ap_if} all_sta").stdout)

    def revoke_all_portal_sessions(self) -> None:
        self.t.run("nft flush set inet tcu_hotspot authorized")

    def bt_connected(self, addr: str) -> bool:
        info = parse_bluetoothctl_info(self.t.run(f"bluetoothctl info {q(addr)}").stdout)
        return info.get("Connected") == "yes"

    def service_active(self, name: str) -> bool:
        return self.t.run(f"systemctl is-active --quiet {q(name)}").ok

    def collect_logs(self, since: str = "-15min") -> str:
        return self.t.run(f"journalctl --no-pager -o short-monotonic --since {q(since)}", timeout=60).stdout


class HeadUnit:
    """Secondary head unit (Linux, wpa_supplicant) acting as a hotspot client."""

    def __init__(self, t: Transport, iface: str = "wlan0", supports_sae: bool = True):
        self.t = t
        self.iface = iface
        self.supports_sae = supports_sae
        self._mac: str | None = None

    def _wpa(self, *args: str) -> str:
        return self.t.run(f"wpa_cli -i {self.iface} " + " ".join(q(a) for a in args)).stdout.strip()

    @property
    def mac(self) -> str:
        if self._mac is None:
            self._mac = self.t.run(f"cat /sys/class/net/{self.iface}/address").stdout.strip().lower()
        return self._mac

    def status(self) -> dict[str, str]:
        return parse_kv(self._wpa("status"))

    def connect(self, ssid: str, passphrase: str, security: str, timeout: float = 20) -> ConnectResult:
        self._wpa("remove_network", "all")
        net = self._wpa("add_network")
        settings = {"ssid": f'"{ssid}"', "psk": f'"{passphrase}"', **SUPPLICANT_SECURITY[security]}
        if not self.supports_sae:
            settings["key_mgmt"] = "WPA-PSK"
        for k, v in settings.items():
            if self._wpa("set_network", net, k, v) != "OK":
                return ConnectResult(False, 0, reason=f"set_network {k} rejected")
        t0 = time.monotonic()
        self._wpa("select_network", net)
        while time.monotonic() - t0 < timeout:
            st = self.status()
            if st.get("wpa_state") == "COMPLETED":
                # DHCP is part of "connected" from the passenger's view
                if self.t.run(f"udhcpc -i {self.iface} -n -q -t 5", timeout=15).ok:
                    return ConnectResult(True, (time.monotonic() - t0) * 1000,
                                         st.get("bssid", ""), st.get("key_mgmt", ""))
                return ConnectResult(False, 0, reason="no DHCP lease")
            time.sleep(0.2)
        return ConnectResult(False, 0, reason=f"timeout in {self.status().get('wpa_state')}")

    def disconnect(self) -> None:
        self._wpa("disconnect")

    def http_probe(self, url: str) -> tuple[int, str]:
        r = self.t.run(f"curl -s -o /dev/null -m 10 -w '%{{http_code}} %{{redirect_url}}' {q(url)}")
        return parse_curl_probe(r.stdout)

    def accept_portal(self, portal_url: str) -> bool:
        page = self.t.run(f"curl -s -m 10 {q(portal_url)}").stdout
        m = re.search(r'name="csrf" value="([^"]+)"', page)
        if not m:
            return False
        u = urlparse(portal_url)
        accept = f"{u.scheme}://{u.netloc}/accept"
        code = self.t.run(f"curl -s -o /dev/null -w '%{{http_code}}' -d csrf={q(m.group(1))} {q(accept)}").stdout
        return code.strip() == "200"

    def download_mb(self, url: str, max_mb: int) -> float:
        r = self.t.run(f"curl -s -o /dev/null -m 120 -r 0-{max_mb * 1_000_000 - 1} "
                       f"-w '%{{size_download}}' {q(url)}", timeout=130)
        try:
            return float(r.stdout.strip() or 0) / 1_000_000
        except ValueError:
            return 0.0


class ProgrammableAP:
    """Linux AP (hostapd) behind a programmable RF attenuator."""

    def __init__(self, t: Transport, bssid: str, conf: str = "/etc/hostapd/hostapd.conf",
                 attenuator_cmd: str | None = None):
        self.t = t
        self.bssid = bssid.lower()
        self.conf = conf
        self.attenuator_cmd = attenuator_cmd  # e.g. "attn-cli --port 2 set {db}"

    def configure(self, ssid: str, passphrase: str, security: str, channel: int) -> None:
        conf = _hostapd_overrides(ssid, passphrase, security, channel)
        conf.update({"ieee80211r": "1", "mobility_domain": "a1b2", "ft_over_ds": "0"})
        _apply_hostapd_conf(self.t, self.conf, conf)
        self.t.run("systemctl restart hostapd", timeout=60).check()

    def set_attenuation(self, db: float) -> None:
        if not self.attenuator_cmd:
            raise RuntimeError("no attenuator configured for this AP")
        self.t.run(self.attenuator_cmd.format(db=db)).check()


class Sniffer:
    """Monitor-mode capture host with tshark for post-analysis."""

    def __init__(self, t: Transport, iface: str = "mon0"):
        self.t = t
        self.iface = iface
        self.pcap = ""

    def start(self, channel: int, name: str) -> None:
        self.pcap = f"/tmp/{name}.pcap"
        self.t.run(f"iw dev {self.iface} set channel {channel}").check()
        self.t.run(f"nohup tcpdump -i {self.iface} -w {self.pcap} -U >/dev/null 2>&1 & "
                   f"echo $! > /tmp/sniffer.pid").check()

    def stop(self) -> str:
        self.t.run("kill $(cat /tmp/sniffer.pid) 2>/dev/null; sleep 0.5")
        return self.pcap

    def _times(self, display_filter: str) -> list[float]:
        r = self.t.run(f"tshark -r {self.pcap} -Y {q(display_filter)} -T fields -e frame.time_epoch",
                       timeout=120)
        return [float(x) for x in r.stdout.split()]

    def count(self, display_filter: str) -> int:
        return len(self._times(display_filter))

    def roam_time_ms(self, sta: str) -> float | None:
        """Reassociation Request -> EAPOL 4/4 (or Reassoc Response with FT)."""
        req = self._times(f"wlan.sa == {sta} && wlan.fc.type_subtype == 0x02")
        if not req:
            return None
        done = self._times(f"(wlan.sa == {sta} && wlan_rsna_eapol.keydes.msgnr == 4) || "
                           f"(wlan.da == {sta} && wlan.fc.type_subtype == 0x03)")
        after = [t for t in done if t >= req[-1]]
        return (after[0] - req[-1]) * 1000 if after else None
