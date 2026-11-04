# wifi_manager — Wi-Fi station manager for embedded Linux (C++17)

`wifimgrd` is a small daemon that keeps an embedded Linux device connected
to the best available saved Wi-Fi network. `wifictl` is its command-line
client. There are no dependencies beyond the C++17 standard library and
Linux: no GLib, D-Bus, NetworkManager or libwpa_client. It runs on a
busybox/systemd Yocto image next to `wpa_supplicant` and `udhcpc`.

```
            ┌──────────── wifimgrd (one epoll thread) ─────────────┐
 wifictl ──►│ ControlServer ─► CommandHandler ─► ConnectionManager │
 (UNIX      │   JSON lines         auth (SO_PEERCRED)   │  ▲  │     │
  stream)   │                                           │  │  │     │
            │ ProfileStore ◄──────── saved networks ────┘  │  │     │
            │ (0600, atomic)                               │  │     │
            │ WpaSupplicant ◄── commands / events ─────────┘  │     │
            │ ProcessDhcpClient ◄── start/stop, exit ─────────┘     │
            │ NetlinkMonitor ── IPv4 address added/removed ─────────┘
            └───┬──────────────────────────┬─────────────────────────┘
                │ ctrl_iface (UNIX dgram)  │ fork/exec
          wpa_supplicant              udhcpc -s udhcpc-hook
                │ nl80211                  └─► `wifictl dhcp-event bound <ip>`
             wlan0 driver
```

## What it does

* **Network selection** — joins the saved network with the highest
  priority, then the strongest signal (with a small 5/6 GHz bonus), ignoring
  BSSs below `min_rssi`. Ties break deterministically.
* **Security-downgrade protection** — a network saved as WPA2/WPA3 is never
  joined if an AP with the same SSID advertises weaker security (e.g. an
  open "evil twin"). Transition-mode (WPA2+WPA3) APs are accepted.
* **Failure handling** — per-SSID exponential backoff after failed
  attempts (timeouts, DHCP failure, AP rejection); a network whose key is
  rejected `max_auth_failures` times is suspended until its credentials are
  updated, instead of hammering the AP or locking an 802.1X account.
* **Link loss** — wpa_supplicant is given `connect_timeout` to reassociate
  to the same network (it handles BSS roaming inside an ESS); after that the
  manager fails over to the next best network.
* **Roaming between SSIDs** — periodic background scans switch to a
  higher-priority network when it appears; when the signal drops below
  `roam_rssi_threshold`, a network `roam_hysteresis` dB stronger is joined.
* **DHCP** — supervises `udhcpc`; the hook applies the lease with iproute2
  and reports it. rtnetlink provides an independent "address configured"
  signal (and makes static/externally managed addressing work with
  `dhcp_command =`).
* **Resilience** — survives wpa_supplicant restarts and crashes (event
  socket errors, `CTRL-EVENT-TERMINATING`, and a PING health check for
  SIGKILL), DHCP client crashes and USB adapter re-plugs.
* **Saved networks** — WPA/WPA2-PSK, WPA3-SAE, WPA2/WPA3 transition,
  802.1X (PEAP/TTLS with CA validation), open and hidden networks. Stored
  0600 with atomic write (temp + fsync + rename + dir fsync) so a power cut
  never leaves a truncated file; SSIDs and passphrases with any bytes are
  escaped safely.
* **Control socket** — line-based requests, JSON replies, `monitor` event
  stream. Read-only commands for anyone who can open the socket; changes
  only for root or the daemon's uid (checked with `SO_PEERCRED`). Secrets
  are never returned or logged.

## Build and test

```sh
cmake -S . -B build -DWIFIMGR_WARNINGS_AS_ERRORS=ON
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Options: `-DWIFIMGR_SANITIZE=ON` (ASan + UBSan), `-DWIFIMGR_BUILD_TESTS=OFF`.

Two test suites run under ctest:

* `unit_tests` — 70 tests of the parsers, profile store, selector, the
  connection state machine (against fake supplicant/DHCP/clock, so every
  timeout path is covered deterministically), command handling and the
  event loop.
* `e2e_tests` — the real `wifimgrd` and `wifictl` binaries against
  `tests/e2e/fake_wpa_supplicant.py`, a simulator speaking the real
  wpa_supplicant control-socket protocol (scans, key checking, events, link
  loss, termination) plus a fake DHCP client that reports leases like the
  production hook. Covers connect, wrong key, hidden/open networks,
  priority failover, link recovery, evil-twin refusal, supplicant restart
  and crash, DHCP failure, persistence across daemon restarts, `monitor`,
  and protocol abuse. Needs only `python3`.

## Usage

```sh
wifictl scan
wifictl connect "Depot WiFi" --psk -          # prompts without echo
wifictl add Yard --psk container-77 --priority 5
wifictl add Corp --security wpa-eap --identity alice --password - --ca-cert /etc/ssl/corp-ca.pem
wifictl add Stealth --psk invisible-1 --hidden
wifictl status
wifictl networks
wifictl monitor
wifictl disconnect      # stays disconnected until `connect` or `reconnect`
wifictl forget Yard
wifictl --json status   # machine-readable output
```

```
$ wifictl status
State:       connected
SSID:        Depot WiFi
BSSID:       02:00:00:00:00:01
IP address:  192.168.50.23
Signal:      -52 dBm
Frequency:   2437 MHz
Autoconnect: on
Saved:       2 network(s)
```

### Control protocol

One request per line (`"`-quoted words, `\` escapes inside quotes), one JSON
object per reply line:

```
> connect "Depot WiFi" psk "forklift-42"
< {"ok":true,"status":{"state":"connected","ssid":"Depot WiFi","ip":"192.168.50.23",...}}
> monitor
< {"ok":true,"monitoring":true}
< {"event":"state","data":{"state":"connecting",...}}
```

Commands: `ping version status results networks monitor scan add connect
forget disconnect reconnect autoconnect dhcp-event`. See
`include/wifimgr/commands.hpp`.

## Deployment

| File | Installed to |
|---|---|
| `wifimgrd`, `wifictl` | `/usr/sbin`, `/usr/bin` |
| `config/wifimgr.conf` | `/etc/wifimgr/wifimgr.conf` (all keys documented inline) |
| `config/wpa_supplicant-wlan0.conf` | `/etc/wpa_supplicant/` — no networks; wifimgrd owns them |
| `scripts/udhcpc-hook` | `/usr/libexec/wifimgr/udhcpc-hook` |
| `systemd/wifimgrd.service` | after `wpa_supplicant@wlan0.service`, hardened, minimal capabilities |
| saved networks | `/var/lib/wifimgr/networks.conf` (persistent data partition on a read-only rootfs) |

Yocto: `meta-telematics/recipes-connectivity/wifi-manager/wifi-manager_git.bb`.
Add `wifi-manager` to `IMAGE_INSTALL` of the image that should run it.

`wifimgrd --check-config` validates the configuration (used by
`ExecStartPre`); unknown keys and out-of-range values are rejected rather
than silently ignored.

## Source layout

| Path | Contents |
|---|---|
| `include/wifimgr/connection_manager.hpp` | the state machine (diagram in the header) |
| `include/wifimgr/supplicant.hpp`, `wpa_ctrl.hpp`, `wpa_parser.hpp` | wpa_supplicant control interface client and parsers |
| `include/wifimgr/network_selector.hpp` | candidate selection and failure backoff |
| `include/wifimgr/profile_store.hpp` | saved networks persistence |
| `include/wifimgr/dhcp_client.hpp`, `netlink_monitor.hpp` | addressing |
| `include/wifimgr/event_loop.hpp` | epoll, timers, signalfd, child reaping |
| `include/wifimgr/control_server.hpp`, `commands.hpp`, `control_client.hpp` | control socket |
| `apps/` | `wifimgrd` and `wifictl` |
| `tests/` | unit tests, fakes, `e2e/` simulator and end-to-end suite |

## Limitations

* Station mode on one interface. Access-point mode is handled separately
  (`src/hotspot` and hostapd in this repository).
* IPv4 addressing only is tracked; IPv6 SLAAC works at the kernel level but
  is not part of the "connected" decision.
* `SCAN_RESULTS` is read in one datagram; wpa_supplicant truncates very long
  lists (roughly 60+ BSSs), in which case some entries are missing.
* 802.1X supports PEAP and TTLS with password authentication; EAP-TLS
  (client certificates) is not wired up yet.
* On a daemon stop the DHCP client is terminated and releases its lease.
