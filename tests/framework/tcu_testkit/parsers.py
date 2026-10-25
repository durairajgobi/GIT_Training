# SPDX-License-Identifier: MIT
"""Parsers for the CLI output the framework scrapes from lab devices."""
from __future__ import annotations

import re


def parse_hostapd_all_sta(text: str) -> dict[str, dict[str, str]]:
    """``hostapd_cli all_sta``: a MAC line followed by key=value lines."""
    stations: dict[str, dict[str, str]] = {}
    current: dict[str, str] | None = None
    for line in text.splitlines():
        line = line.strip()
        if re.fullmatch(r"[0-9a-fA-F]{2}(:[0-9a-fA-F]{2}){5}", line):
            current = stations.setdefault(line.lower(), {})
        elif current is not None and "=" in line:
            k, v = line.split("=", 1)
            current[k] = v
    return stations


def parse_kv(text: str) -> dict[str, str]:
    """``wpa_cli status`` and similar key=value dumps."""
    out = {}
    for line in text.splitlines():
        if "=" in line:
            k, v = line.split("=", 1)
            out[k.strip()] = v.strip()
    return out


def parse_bluetoothctl_info(text: str) -> dict[str, str]:
    """``bluetoothctl info <addr>``: indented 'Key: value' lines."""
    out = {}
    for line in text.splitlines():
        m = re.match(r"^\s+([A-Za-z ]+):\s*(.*)$", line)
        if m:
            out.setdefault(m.group(1).strip(), m.group(2).strip())
    return out


def parse_curl_probe(text: str) -> tuple[int, str]:
    """Output of ``curl -w '%{http_code} %{redirect_url}'``."""
    parts = text.strip().split(" ", 1)
    try:
        code = int(parts[0])
    except (ValueError, IndexError):
        return 0, ""
    return code, parts[1] if len(parts) > 1 else ""
