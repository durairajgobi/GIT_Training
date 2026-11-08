# ble_bluez — BLE communication app in C++ for BlueZ

A C++17 library and command-line tool (`ble-app`) that drives **BlueZ 5**
(`bluetoothd`) over its **D-Bus API** using GLib/GIO (GDBus). It does not use
raw HCI sockets or the deprecated `libbluetooth`, so it works alongside
`bluetoothctl` and the rest of the system.

It covers both BLE roles:

| Role | What it does |
|---|---|
| **Central** | Discovers devices, **registers a pairing agent**, pairs/bonds, trusts, connects (with retry), waits for GATT service discovery, then reads, writes or subscribes to characteristics. `auto` connects to every discoverable device that matches a filter. |
| **Peripheral** | Makes the adapter discoverable/pairable, **registers a GATT application** (`GattManager1.RegisterApplication`) and an **LE advertisement** (`LEAdvertisingManager1.RegisterAdvertisement`) so phones and other centrals can find it and connect. |

## Build

Dependencies: CMake ≥ 3.16, a C++17 compiler, GLib/GIO ≥ 2.56 (`libglib2.0-dev`).
At run time you need BlueZ ≥ 5.50 (`bluetoothd`). For the peripheral role, run
it with `--experimental` on BlueZ < 5.48.

```bash
sudo apt install build-essential cmake pkg-config libglib2.0-dev bluez
```

```bash
cmake -S . -B build && cmake --build build -j
```

```bash
ctest --test-dir build --output-on-failure
```

```bash
sudo cmake --install build
```

The tests need no Bluetooth hardware:

* `utils` covers address, UUID and hex helpers.
* `integration` starts a private `dbus-daemon` and `tests/fake_bluez`, a
  minimal stand-in for `bluetoothd` that calls back into the agent, the GATT
  application and the advertisement the way the real daemon does. It then runs
  every `ble-app` command end to end. It is skipped when `dbus-daemon` is
  missing.

### Permissions

Talking to `org.bluez` usually needs root. To run as a normal user, install the
D-Bus policy and add yourself to the `bluetooth` group:

```bash
sudo install -m 644 config/ble-app.conf /etc/dbus-1/system.d/
```

```bash
sudo usermod -aG bluetooth "$USER"
```

## Usage

```
ble-app [-i hciX] [--agent CAP] [--interactive] <command> [options]
```

| Command | Description |
|---|---|
| `scan [-t SEC] [--uuid UUID]... [--rssi DBM] [--name PREFIX]` | List discoverable LE devices (default 10 s) |
| `connect MAC [-t SEC] [--no-pair] [--stay]` | Find, pair, trust, connect and print the GATT database |
| `auto [-t SEC] [--uuid UUID]... [--rssi DBM] [--name PREFIX] [--max N]` | Connect to every matching discoverable device (default 30 s) |
| `read MAC UUID` | Read a characteristic |
| `write MAC UUID HEX [--no-response]` | Write a characteristic (`"01 02"`, `0102`, `01:02`, `0x0102`) |
| `notify MAC UUID [-t SEC]` | Subscribe and print notifications/indications |
| `disconnect MAC` / `remove MAC` | Disconnect / forget (unpair) a device |
| `serve [--name NAME]` | Run as a peripheral (GATT server + advertisement) |

UUIDs can be 16-bit (`2a19`), 32-bit or full 128-bit.

**Pairing agent.** Every command registers `org.bluez.Agent1` at
`/com/example/ble/agent` and makes it the default agent. With no options it
accepts every request, which suits headless or embedded use. `--interactive`
prompts on the terminal for passkey confirmation, passkey/PIN entry and service
authorisation. `--agent` sets the IO capability (`KeyboardDisplay` by default;
`serve` defaults to `NoInputNoOutput`, i.e. Just Works).

### Examples

```bash
ble-app scan -t 15 --uuid 180f --rssi -80
```

```bash
ble-app connect C4:7C:8D:6A:12:34 --interactive
```

```bash
ble-app read C4:7C:8D:6A:12:34 2a19
```

```bash
ble-app write C4:7C:8D:6A:12:34 12345678-1234-5678-1234-56789abcdef1 "68 65 6c 6c 6f"
```

```bash
ble-app notify C4:7C:8D:6A:12:34 2a37
```

```bash
ble-app auto -t 60 --name Sensor- --max 5
```

```bash
sudo ble-app serve --name MyGateway
```

`serve` exposes:

| Service | Characteristic | Flags |
|---|---|---|
| Battery `180F` | Battery Level `2A19` (counts down every 5 s) | read, notify |
| Device Information `180A` | Manufacturer `2A29`, Model `2A24` | read |
| Echo `12345678-…-def0` | `…-def1` echo buffer | read, write, write-without-response, notify |
| | `…-def2` command (0–2, otherwise rejected) | write |

Test it from a phone with nRF Connect or LightBlue, or from another Linux box:

```bash
ble-app notify <gateway-MAC> 2a19
```

## Library

Link `ble_bluez` (CMake target) and include `ble/…`:

| Header | Class | Purpose |
|---|---|---|
| `ble/ble_manager.hpp` | `BleManager` | System bus, main loop, live mirror of devices / services / characteristics, event subscriptions |
| `ble/adapter.hpp` | `Adapter` | Power, alias, discoverable/pairable, discovery filter, start/stop discovery, remove device |
| `ble/device.hpp` | `Device`, `DeviceConnector` | Async pair/connect/disconnect; state machine pair → trust → connect (retry) → services resolved |
| `ble/agent.hpp` | `Agent` | `org.bluez.Agent1` with overridable hooks, registered via `AgentManager1` |
| `ble/gatt_client.hpp` | `GattClient` | Read / write / start-stop notify on a remote characteristic |
| `ble/gatt_server.hpp` | `GattApplication`, `Advertisement` | Local GATT services and LE advertising |
| `ble/utils.hpp` | — | Address, UUID and hex helpers (no GLib dependency) |

```cpp
ble::BleManager mgr("hci0");
ble::Agent agent(mgr.connection(), ble::Agent::Capability::NoInputNoOutput);
agent.registerAgent();

mgr.subscribeDevices([&](ble::DeviceEvent ev, const ble::DeviceInfo& d) {
    if (ev == ble::DeviceEvent::Added && d.name == "MySensor") { /* connect */ }
});
mgr.subscribeValues([](const ble::GattCharacteristicInfo& c, const std::vector<uint8_t>& v) {
    std::printf("%s -> %s\n", c.uuid.c_str(), ble::toHex(v).c_str());
});
mgr.adapter().setDiscoveryFilter({});
mgr.adapter().startDiscovery();
mgr.run();
```

### Design notes

* Anything that can make `bluetoothd` call back into this process (`Pair`,
  `RegisterApplication`, `RegisterAdvertisement`) is an **asynchronous** D-Bus
  call. A synchronous call would block the main loop that has to serve the
  agent or `GetManagedObjects` request, and both sides would deadlock until
  the timeout.
* Discovery is stopped before connecting; LE connection setup is much more
  reliable when the controller is not scanning. Connect is retried
  (`le-connection-abort-by-local` and similar errors are common).
* The device table is built from one `GetManagedObjects` snapshot and then
  kept current through `InterfacesAdded`/`InterfacesRemoved`/`PropertiesChanged`.
  The table never polls.
