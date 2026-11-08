# wifi6_comm: Wi-Fi 6 communication application in C++ for embedded Linux

A small C++17 Wi-Fi 6 (802.11ax) station application for embedded Linux
targets such as NXP i.MX8, Raspberry Pi and other Arm SoMs. It:

* **joins** WPA3-SAE, WPA2/WPA3 transition, WPA2-PSK, OWE or open networks
  through `wpa_supplicant`. It prefers **6 GHz (Wi-Fi 6E)** and **5 GHz HE**
  access points and enforces 6 GHz rules (SAE/OWE only, PMF, SAE-H2E);
* **detects HE capabilities** by parsing the beacon IEs (HE Capabilities and
  HE Operation): Wi-Fi 6 support, TWT responder support and BSS colour;
* **roams** between BSSs of the same network, using an RSSI EWMA with
  hysteresis and background scans. It recovers from link loss with
  jittered exponential backoff and stops after repeated authentication
  failures, so a bad password doesn't keep hitting the AP;
* **negotiates Target Wake Time (TWT)** so the radio can sleep between
  telemetry bursts. The interval is encoded as `mantissa × 2^exponent` µs;
* **streams telemetry and accepts commands** over UDP. Frames carry a
  CRC32, commands are acknowledged and retransmitted, and duplicates are
  dropped. Packets are tagged with WMM access categories (RFC 8325 DSCP
  values plus mac80211 `SO_PRIORITY`).

## Embedded design rules

| Rule | How it is met |
|---|---|
| No heap use at runtime | Fixed-size arrays, `SpscRing<T,N>`, static frame buffers |
| No exceptions / RTTI | Built with `-fno-exceptions -fno-rtti`; every call returns `Status` |
| No FPU needed | Integer Q4 EWMA, integer TWT maths |
| Non-blocking, single thread | `tick(now)` state machines, driven by one `poll()` loop |
| Testable without hardware | `IWifiDriver` HAL with `WpaCtrlDriver` (target) and `SimDriver` (host) |
| Secrets handled carefully | Passphrase read from env/file, never logged, wiped with `secureZero` |
| Small footprint | About 42 KB `.text` for the whole application (x86-64, `-Os`, gc-sections) |

## Architecture

```
            app/main.cpp  (event loop, telemetry, command handler)
               │                               │
   ConnectionManager                       MessageChannel ── frame.cpp (CRC32 framing)
   ├─ ApSelector  (band/HE scoring, roaming)   │
   ├─ LinkMonitor (RSSI EWMA + hysteresis)  IDatagramTransport
   ├─ Backoff     (jittered exponential)       └─ UdpTransport (WMM / DSCP, SO_BINDTODEVICE)
   └─ TwtParams   (802.11ax TWT encoding)
               │
          IWifiDriver (HAL)
          ├─ WpaCtrlDriver ── wpa_supplicant ctrl socket ── nl80211 ── Wi-Fi 6 chip
          │    └─ wpa_parse.cpp / ie_parser.cpp (pure parsers)
          └─ SimDriver (host tests, --sim demo)
```

Connection state machine: `Idle → Scanning → Connecting → Connected`.
Failures go to `Backoff`, which leads back to `Scanning`. Repeated
authentication failures go to `Failed`.

## Build

```bash
cmake -S . -B build && cmake --build build -j && ctest --test-dir build --output-on-failure
```

or without CMake:

```bash
make && make test
```

Cross-compile for aarch64:

```bash
cmake -S . -B build-arm64 -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-aarch64-linux-gnu.cmake
```

In a Yocto SDK, source the SDK environment script and run a plain
`cmake -S . -B build`.

## Run

Host demo with a simulated radio. Telemetry and commands loop back to the
process itself, and the 6 GHz AP fades after 8 s to show roaming:

```bash
./build/wifi6-comm --sim
```

On target (wpa_supplicant running with `ctrl_interface=/var/run/wpa_supplicant`):

```bash
export WIFI6_PASSPHRASE='…'
./build/wifi6-comm --iface wlan0 --ssid FleetNet --security sae \
    --peer 10.20.0.1:5005 --ac vi --twt-interval-ms 1000
```

Commands the back-end can send (`MsgType::Command`, reliable):
`PING`, `STATUS`, `SET_PERIOD <ms>`. Each reply is a `Response` frame.

Telemetry payload (big-endian, v1): `ver u8 | uptime_s u32 | rssi i16 |
rssi_avg i16 | freq u16 | channel u16 | band u8 | state u8 | twt u8 | mcs u8 |
rate_kbps u32 | tx_frames u32 | retransmits u32 | roams u32`.

A sample unit for systemd is in `systemd/wifi6-comm.service`.

## Wire format

```
magic "W6" (2) | ver (1) | type (1) | flags (1) | rsv (1) | seq (2) | len (2) | payload (≤1200) | CRC32 (4)
```

`flags bit0` means the sender wants an ACK. The receiver replies with an
`Ack` frame that carries the same `seq`. The sender retransmits after
250, 500 and 1000 ms and then reports the frame as undelivered.

## Notes and limits

* TWT goes through `TWT_SETUP`, which needs wpa_supplicant 2.10 or later
  and a driver that implements it. If either is missing, the station
  carries on without TWT and logs that it isn't supported.
* 802.1X/EAP-Enterprise networks are recognised but not supported.
* SSIDs with non-printable bytes are compared as wpa_supplicant prints them
  (escaped).
