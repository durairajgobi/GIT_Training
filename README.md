# TCU — Telematics Control Unit software

Embedded Linux services, Yocto layer and test framework for a truck
telematics unit on NXP i.MX8: eCall, LTE, Bluetooth hands-free, passenger
Wi-Fi hotspot, power management, TrustZone-backed device identity and
mTLS.

Design document: [docs/DESIGN.md](docs/DESIGN.md)

## Layout

| Path | Contents |
|---|---|
| `src/ecall` | eCall state machine, MSD encoder, `ecalld` |
| `src/lte` | AT parser, LTE connection state machine, `ltecm` |
| `src/bt` | Reconnection policy, HFP 1.7 codec negotiation, `btmgr` (BlueZ D-Bus) |
| `src/pm` | Power policy, `tcu-pmd` |
| `src/security` | AF_ALG (kernel crypto) helpers, mTLS + OCSP client, `tcu-hash`, `tcu-tls-probe` |
| `src/hotspot` | nftables per-passenger policy, captive portal, bring-up script |
| `optee/` | OP-TEE keystore TA and client |
| `meta-telematics/` | Yocto layer: distro, images, kernel/U-Boot/OP-TEE/BlueZ/OpenSSH/AAA recipes |
| `docker/` | Container images and compose stack |
| `systemd/`, `config/` | Service units and default configuration |
| `tests/` | C unit tests, Python unit tests, HIL framework (`tcu_testkit`) and suites |

## Host build and tests

```sh
cmake -S . -B build && cmake --build build -j
ctest --test-dir build --output-on-failure
pip install pytest
python3 -m pytest                       # unit + HIL suite on the simulated testbed
```

Dependencies: CMake ≥ 3.16, a C11 compiler, OpenSSL ≥ 1.1.1 and GLib/GIO
(for `btmgr`; it is skipped automatically if GIO is missing).

## Running the HIL suite on a bench

```sh
python3 -m pytest tests/hil --testbed tests/testbeds/lab1.json --stress-iterations 100
```

## Yocto

```sh
TEMPLATECONF=../meta-telematics/conf/templates/default source poky/oe-init-build-env build-tcu
bitbake tcu-image        # production
bitbake tcu-image-dev    # lab (debug tools, telnet)
```
