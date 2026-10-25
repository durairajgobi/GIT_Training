# SPDX-License-Identifier: MIT
"""Exercises the real HTTP handler on localhost with a fake neighbour table."""
import json
import threading
import urllib.error
import urllib.request
from http.server import ThreadingHTTPServer

import pytest

import captive_portal as cp

ARP = """IP address       HW type     Flags       HW address            Mask     Device
192.168.50.12    0x1         0x2         02:00:00:00:00:02     *        ap0
192.168.50.13    0x1         0x0         00:00:00:00:00:00     *        ap0
"""


def test_mac_from_arp():
    assert cp.mac_from_arp("192.168.50.12", ARP) == "02:00:00:00:00:02"
    assert cp.mac_from_arp("192.168.50.13", ARP) is None  # incomplete entry
    assert cp.mac_from_arp("192.168.50.99", ARP) is None


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, *a, **k):
        return None


@pytest.fixture
def server():
    authorized = []
    portal = cp.Portal("http://portal.test/terms", 750,
                       resolve_mac=lambda ip: "02:00:00:00:00:02" if ip == "127.0.0.1" else None,
                       authorize=authorized.append, csrf_token="tok")
    httpd = ThreadingHTTPServer(("127.0.0.1", 0), portal.handler())
    threading.Thread(target=httpd.serve_forever, daemon=True).start()
    yield f"http://127.0.0.1:{httpd.server_address[1]}", authorized
    httpd.shutdown()


def get(url):
    opener = urllib.request.build_opener(NoRedirect)
    try:
        r = opener.open(url, timeout=5)
        return r.status, dict(r.headers), r.read()
    except urllib.error.HTTPError as e:
        return e.code, dict(e.headers), e.read()


def post(url, data):
    try:
        r = urllib.request.urlopen(urllib.request.Request(url, data=data.encode()), timeout=5)
        return r.status
    except urllib.error.HTTPError as e:
        return e.code


def test_probe_redirects_to_portal(server):
    base, _ = server
    code, headers, _ = get(base + "/generate_204")
    assert code == 302
    assert headers["Location"] == "http://portal.test/terms"


def test_terms_page_shows_quota_and_csrf(server):
    base, _ = server
    code, _, body = get(base + "/terms")
    assert code == 200
    assert b"750 MB" in body and b'value="tok"' in body


def test_captive_api_rfc8908(server):
    base, _ = server
    code, headers, body = get(base + "/api/captive")
    assert code == 200
    assert headers["Content-Type"] == "application/captive+json"
    assert json.loads(body) == {"captive": True, "user-portal-url": "http://portal.test/terms"}


def test_accept_requires_csrf_and_authorises_once(server):
    base, authorized = server
    assert post(base + "/accept", "csrf=wrong") == 403
    assert authorized == []
    assert post(base + "/accept", "csrf=tok") == 200
    assert post(base + "/accept", "csrf=tok") == 200
    assert authorized == ["02:00:00:00:00:02"]
    assert json.loads(get(base + "/api/captive")[2])["captive"] is False
