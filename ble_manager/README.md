# blemgr — BLE Manager for Embedded Linux (C++17, BlueZ)

`blemgr` is a Bluetooth Low Energy manager for embedded Linux. It drives
BlueZ (≥ 5.50) over D-Bus. It is split into a small library and the
`blemgrd` daemon.

| Role | Features |
|---|---|
| Central | Filtered scanning; one connection attempt at a time with an optional scan pause; per-device state machine; connect timeouts; auto-reconnect with exponential back-off and jitter; notification subscriptions restored after every reconnect; GATT read/write/notify; bonding through a built-in pairing agent |
| Peripheral | LE advertising (name, service UUIDs, manufacturer and service data, TX power); a local GATT server with read, write and notify callbacks |
| Daemon | INI config; `systemd` `Type=notify` unit; `SIGHUP` status dump; interactive shell (`-i`) |

## Architecture

```
            +--------------------- blemgrd ----------------------+
            |  config.cpp   shell.cpp   LogListener   telemetry  |
            +--------------------------+-------------------------+
                                       | thread-safe async API
+------------------------------------- v -------------------------------------+
| BleManager  (policy, stack-agnostic, unit-tested)                           |
|   device registry + snapshots   connect queue   ReconnectPolicy              |
|   per-device ConnState machine  scan reconciler  subscription restore        |
+-------------+------------------------------------+--------------------------+
              | IBleBackend / IBackendEvents       | IScheduler
+-------------v------------------+      +----------v-----------+
| BluezBackend (GDBus, pimpl)    |      | GlibScheduler        |
|  ObjectManager client, Adapter1|      |  private GMainContext|
|  Device1, GattCharacteristic1  |      |  on its own thread   |
|  Agent1 (exported)             |      +----------------------+
|  LEAdvertisement1 (exported)   |
|  GATT application (exported)   |
+-------------+------------------+
              | D-Bus (system bus)
         bluetoothd  ->  kernel HCI  ->  controller
```

* **Threading.** All the manager logic and all D-Bus traffic run on one
  event-loop thread (`GlibScheduler`). Public `BleManager` methods are
  thread-safe: they post work to that loop and report back through
  callbacks. `devices()`, `adapterState()` and `scanning()` return
  snapshots protected by a mutex.
* **Testability.** `BleManager` depends only on `IBleBackend` and
  `IScheduler`. The unit tests use a fake backend you script step by step
  and a virtual clock, so timeouts and back-off run deterministically and
  instantly.

### Connection state machine

```
             connect()                       slot free
 Disconnected ---------> Queued --------------------------> Connecting
      ^                    ^  ^                               |   |   \  NotFound
      |      back-off timer|  | advertisement seen            |   |    \
      |                    |  +------------- WaitingAdvert <--+---|-----+
      |                WaitingRetry <---- fail / timeout -----+   | link up
      |                    ^                                      v
      |                    +------ link lost (managed) ------ Discovering
      |                                                           | services resolved
      +---- disconnect() / link lost (unmanaged) / give up ------ Ready
```

* **One connection attempt at a time.** Many controllers handle only one
  pending LE Create Connection. While an attempt is in flight, discovery
  pauses (`pause_scan_during_connect`). `max_connections` caps how many
  links are up at once.
* **Device not yet known.** If BlueZ has never seen the device (not bonded,
  not cached), the manager moves it to `WaitingAdvert` instead of retrying
  blindly. It then runs an unfiltered background scan and connects as soon
  as the device advertises.
* **Stale callbacks.** Each attempt carries a generation counter. A late
  D-Bus reply from an attempt that was superseded (timeout, user
  disconnect) is ignored.
* **Ready.** On entering `Ready`, the manager re-enables every persistent
  subscription. If `pair = true` and the device is not yet bonded, it pairs
  and marks the device trusted.
* **Controller loss.** When the adapter is powered off or removed, the
  manager powers it back on if `power_on = true`. The advertisement and the
  GATT application are registered again once the adapter reappears.

## Layout

| Path | Contents |
|---|---|
| `include/blemgr/` | Public API: `ble_manager.hpp`, `backend.hpp`, `types.hpp`, `bluez_backend.hpp`, `glib_scheduler.hpp`, `config.hpp` |
| `src/` | Stack-independent core: manager, reconnect policy, config parser, types |
| `src/bluez/` | BlueZ backend: GDBus plumbing, central role, pairing agent, peripheral role, GLib scheduler |
| `app/` | `blemgrd` daemon and its interactive shell |
| `tests/` | Unit tests with fakes; `mock_bluez` and the BlueZ end-to-end test |
| `config/` | Reference `blemgrd.conf` and the D-Bus policy |
| `systemd/` | Hardened service unit |

The Yocto recipe is in `meta-telematics/recipes-connectivity/blemgr/`.

## Build and test

Dependencies: CMake ≥ 3.16, a C++17 compiler and GLib/GIO ≥ 2.56.
`dbus-daemon` is needed only for the end-to-end test.

```sh
cmake -S . -B build -DBLEMGR_WARNINGS_AS_ERRORS=ON
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Options: `-DBLEMGR_SANITIZE=ON` builds with ASan and UBSan.
`-DBLEMGR_BUILD_TESTS=OFF` skips the tests.

| Test | What it covers |
|---|---|
| `types`, `reconnect_policy`, `config` | UUID and address normalisation, hex, scan filters, back-off and jitter bounds, config parsing and errors |
| `ble_manager` | 22 policy scenarios against the fake backend: happy path, serialization, `max_connections`, timeout and retry, stale replies, link loss and re-subscribe, user disconnect, `WaitingAdvert`, retry budget, pairing, adapter power loss, incoming links, pruning, shutdown |
| `bluez_e2e` | The real `BluezBackend`, `GlibScheduler` and `BleManager` against `mock_bluez` (a stand-in `bluetoothd`) on a private `dbus-daemon`: power-on, filtered scan, connect, pairing, notifications, read and write, link loss and auto-reconnect, GATT server called back by "BlueZ", advertisement registration, disconnect |

## Running

```sh
blemgrd -c /etc/blemgr/blemgrd.conf          # as a service
blemgrd -i -v                                # interactive, debug logging
blemgrd -i --bus unix:path=/tmp/test-bus     # against another bus (e.g. mock_bluez)
```

Shell session:

```
> scan on 180d
> devices
AA:BB:CC:DD:EE:FF  disconnected  rssi -61  "HRM-42"
> connect AA:BB:CC:DD:EE:FF pair
> sub AA:BB:CC:DD:EE:FF 180d 2a37
> read AA:BB:CC:DD:EE:FF 180d 2a38
read 2a38: 01
> write AA:BB:CC:DD:EE:FF 180d 2a39 01
> info AA:BB:CC:DD:EE:FF
> adv on TCU-BLE
> quit
```

### Configuration

See [`config/blemgrd.conf`](config/blemgrd.conf). Every key is optional.
To keep a device connected, add a section for it:

```ini
[device AA:BB:CC:DD:EE:FF]
auto_reconnect = true
pair = true
subscribe = 180d/2a37
```

With `[peripheral] enabled = true`, the daemon publishes:

* **Device Information** (`0x180A`)
* **Telemetry** (`a0b40001-…`): `a0b40002` holds an uptime counter (read
  and notify). `a0b40003` is a command input (write and write without
  response); empty writes are rejected.

The daemon also advertises the telemetry service.

### Deployment notes

* The unit runs as the unprivileged `blemgr` user in the `bluetooth` group.
  `config/blemgrd-dbus.conf` (installed as
  `/usr/share/dbus-1/system.d/com.tcu.blemgr.conf`) allows it to call
  `org.bluez`.
* Reconnection to bonded devices: the manager marks bonded devices
  trusted. Its own back-off covers the cases that BlueZ's
  `[Policy] ReconnectAttempts` cannot see.
* Advertising and GATT server registration need `bluetoothd` built with LE
  support. BlueZ ≥ 5.51 is recommended for the `type` write option
  (write with or without response).

## Using the library

```cpp
blemgr::GlibScheduler sched;
sched.start();
blemgr::BluezBackend backend({/* adapter = */ "hci0"});
blemgr::BleManager mgr(backend, sched);
mgr.addListener(&myListener);                 // before start()
mgr.start([](const blemgr::Status& s) { /* ... */ });

blemgr::ConnectOptions o;
o.subscriptions = {{"180d", "2a37"}};
mgr.connect("AA:BB:CC:DD:EE:FF", o);
// ... myListener.onNotification(...) fires on the loop thread ...

mgr.stop(done);                               // then sched.stop()
```
