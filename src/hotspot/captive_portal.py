#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Captive portal for the in-truck hotspot.

* Every HTTP request DNAT'ed here from an unauthorised client gets a 302 to
  the terms page, which makes OS captive-portal detection (Android
  generate_204, Apple hotspot-detect.html, Windows connecttest) pop the
  sign-in sheet.
* RFC 8908 Captive Portal API at /api/captive (advertised via DHCP option
  114, RFC 8910) so modern clients don't have to probe.
* Accepting the terms adds the client MAC to the nftables ``authorized`` set.

Runs unprivileged in its container with CAP_NET_ADMIN only.
"""
from __future__ import annotations

import argparse
import html
import json
import logging
import re
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Callable
from urllib.parse import parse_qs

try:  # installed side by side on target
    import hotspot_policy
except ImportError:  # pragma: no cover - when run from the source tree
    import sys

    sys.path.insert(0, str(Path(__file__).resolve().parent))
    import hotspot_policy

log = logging.getLogger("captive-portal")

TERMS_PAGE = """<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Vehicle Wi-Fi</title></head>
<body style="font-family:sans-serif;max-width:32rem;margin:2rem auto;padding:0 1rem">
<h1>Vehicle Wi-Fi</h1>
<p>Your data allowance on this vehicle is <b>{quota} MB</b>.
Usage is shared with other passengers and may be slowed when the
vehicle's mobile signal is weak.</p>
<form method="post" action="/accept">
<input type="hidden" name="csrf" value="{csrf}">
<button type="submit" style="font-size:1.2rem;padding:.6rem 1.2rem">Accept and connect</button>
</form></body></html>
"""

ARP_RE = re.compile(r"^(\d+\.\d+\.\d+\.\d+)\s+\S+\s+\S+\s+([0-9a-fA-F:]{17})\s")


def mac_from_arp(ip: str, arp_text: str | None = None) -> str | None:
    """Resolve a client IP to its MAC via the kernel neighbour table."""
    if arp_text is None:
        try:
            arp_text = Path("/proc/net/arp").read_text()
        except OSError:
            return None
    for line in arp_text.splitlines()[1:]:
        m = ARP_RE.match(line)
        if m and m.group(1) == ip and m.group(2) != "00:00:00:00:00:00":
            return m.group(2).lower()
    return None


class Portal:
    def __init__(
        self,
        portal_url: str,
        quota_mb: int,
        resolve_mac: Callable[[str], str | None] = mac_from_arp,
        authorize: Callable[[str], None] = hotspot_policy.authorize,
        csrf_token: str | None = None,
    ):
        import secrets

        self.portal_url = portal_url
        self.quota_mb = quota_mb
        self.resolve_mac = resolve_mac
        self.authorize = authorize
        self.csrf = csrf_token or secrets.token_urlsafe(16)
        self.authorized: set[str] = set()
        self._lock = threading.Lock()

    def is_authorized(self, ip: str) -> bool:
        mac = self.resolve_mac(ip)
        with self._lock:
            return mac is not None and mac in self.authorized

    def accept(self, ip: str) -> bool:
        mac = self.resolve_mac(ip)
        if not mac:
            log.warning("no neighbour entry for %s", ip)
            return False
        with self._lock:
            if mac not in self.authorized:
                self.authorize(mac)
                self.authorized.add(mac)
                log.info("authorised %s (%s)", mac, ip)
        return True

    def handler(self) -> type[BaseHTTPRequestHandler]:
        portal = self

        class Handler(BaseHTTPRequestHandler):
            server_version = "tcu-portal"

            def log_message(self, fmt, *args):  # route to logging, not stderr
                log.debug("%s " + fmt, self.client_address[0], *args)

            def _send(self, code: int, body: bytes = b"", ctype: str = "text/html; charset=utf-8",
                      headers: dict[str, str] | None = None) -> None:
                self.send_response(code)
                self.send_header("Content-Type", ctype)
                self.send_header("Content-Length", str(len(body)))
                self.send_header("Cache-Control", "no-store")
                for k, v in (headers or {}).items():
                    self.send_header(k, v)
                self.end_headers()
                self.wfile.write(body)

            def do_GET(self):
                ip = self.client_address[0]
                if self.path == "/api/captive":
                    body = json.dumps({
                        "captive": not portal.is_authorized(ip),
                        "user-portal-url": portal.portal_url,
                    }).encode()
                    self._send(200, body, "application/captive+json")
                elif self.path in ("/", "/terms"):
                    page = TERMS_PAGE.format(quota=portal.quota_mb, csrf=html.escape(portal.csrf))
                    self._send(200, page.encode())
                else:
                    # Any other host/path: captive-portal detection probe.
                    self._send(302, headers={"Location": portal.portal_url})

            def do_POST(self):
                if self.path != "/accept":
                    self._send(404)
                    return
                length = min(int(self.headers.get("Content-Length") or 0), 4096)
                form = parse_qs(self.rfile.read(length).decode(errors="replace"))
                if form.get("csrf", [""])[0] != portal.csrf:
                    self._send(403, b"forbidden")
                    return
                if portal.accept(self.client_address[0]):
                    self._send(200, b"<p>Connected. You can close this page.</p>")
                else:
                    self._send(503, b"<p>Please reconnect to the Wi-Fi and try again.</p>")

        return Handler


def main() -> None:
    ap = argparse.ArgumentParser(description="TCU hotspot captive portal")
    ap.add_argument("--bind", default="192.168.50.1")
    ap.add_argument("--port", type=int, default=8080)
    ap.add_argument("--quota-mb", type=int, default=1024)
    args = ap.parse_args()
    logging.basicConfig(level=logging.INFO, format="%(name)s: %(message)s")

    portal = Portal(f"http://{args.bind}:{args.port}/terms", args.quota_mb)
    ThreadingHTTPServer((args.bind, args.port), portal.handler()).serve_forever()


if __name__ == "__main__":
    main()
