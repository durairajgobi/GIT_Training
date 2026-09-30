# Rust in Automotive

This folder contains a small, dependency-free Rust crate with example automotive
software components, plus an overview of where Rust is used in the automotive
industry and project ideas for practice.

## The example crate

| Module | What it shows |
|---|---|
| `src/can.rs` | Classic CAN 2.0 frames (11/29-bit IDs, DLC ≤ 8) and DBC-style signal encode/decode (`physical = raw * factor + offset`) |
| `src/obd2.rs` | OBD-II Mode 01 requests on `0x7DF` and response parsing from `0x7E8..0x7EF` (RPM, speed, coolant temp, throttle, fuel, load) |
| `src/bms.rs` | Battery management: over/under-voltage and temperature faults that latch, cold-charge protection, coulomb-counting SoC, passive cell balancing |
| `src/cruise.rs` | Cruise control: PI controller with output clamping, anti-windup, set-speed limits and brake/clutch disengage |
| `examples/ecu_simulator.rs` | Ties everything together in a simulated drive |

```bash
cd rust-automotive
cargo test                          # unit tests for every module
cargo run --example ecu_simulator   # simulated drive with CAN + OBD-II output
```

Rust features that matter for automotive code, and where they appear here:

* **Invalid states are unrepresentable** – `CanId` is an enum, `State::Fault` carries the reason,
  and a latched fault can only be left through `Bms::clear_fault`.
* **Errors are values** – constructors and parsers return `Result`, so a bad CAN ID or a malformed
  ECU response can't be silently ignored.
* **No undefined behaviour in safe code** – no buffer overruns when slicing frame payloads,
  no data races, no null pointers.
* **Fixed-size data** – frames use `[u8; 8]` rather than heap allocation, the way embedded ECU code would.

## Where Rust is used in automotive

**Safety-qualified toolchains**
* **Ferrocene** (Ferrous Systems) – a Rust compiler qualified for ISO 26262 (ASIL D) and IEC 61508.
* **HighTec Rust compiler** – Rust for Infineon AURIX (TriCore) microcontrollers, aimed at ISO 26262.
* **AdaCore GNAT Pro for Rust** – supported Rust toolchain for high-integrity systems.
* **Safety-Critical Rust Consortium** (Rust Foundation) – industry group working on coding guidelines
  and tooling for safety-critical Rust.

**Software-defined vehicle (Eclipse SDV) projects written in Rust**
* **Eclipse Kuksa** – `kuksa-databroker`, a vehicle signal server based on the COVESA Vehicle Signal Specification (VSS).
* **Eclipse Zenoh** – pub/sub/query protocol used for vehicle-to-cloud and in-vehicle communication.
* **Eclipse uProtocol** – `up-rust`, the Rust implementation of the uProtocol transport abstraction.
* **Eclipse Ankaios** – workload and container orchestration for vehicle HPCs.
* **Eclipse Chariott / Ibeji / Freyja** – service discovery, digital twin and cloud-sync components.

**OEMs and suppliers**
* **Volvo Cars** has publicly described shipping Rust in ECU software (low-power ECU on the EX90).
* **AUTOSAR** has published guidance on using Rust within the AUTOSAR architecture.
* Several startups (e.g. **Veecle**) build Rust-first automotive software platforms.

**Useful open-source crates**
* `socketcan` – Linux SocketCAN bindings; `embedded-can` – CAN traits for `no_std` drivers.
* `can-dbc` / `dbc-codegen` – parse DBC files or generate Rust types from them.
* `ecu_diagnostics`, `automotive_diag` – UDS / KWP2000 / OBD-II diagnostics.
* `embedded-hal`, **Embassy**, **RTIC** – bare-metal and async embedded frameworks for MCUs.
* `probe-rs` – flashing and debugging microcontrollers.

## Project ideas to practice

1. **CAN bus logger** – read `vcan0` with `socketcan`, decode signals from a DBC file, write CSV.
2. **OBD-II dashboard** – talk to an ELM327 adapter over serial and show live RPM/speed in a TUI (`ratatui`).
3. **UDS diagnostic client** – implement ISO-TP multi-frame transport and services `0x10`, `0x22`, `0x27`.
4. **BMS on a microcontroller** – port `bms.rs` to `no_std` with Embassy on an STM32 board.
5. **Adaptive cruise control** – extend `cruise.rs` with a lead-vehicle distance controller and time gap.
6. **VSS data broker client** – subscribe to `Vehicle.Speed` from Eclipse Kuksa over gRPC (`tonic`).
7. **AUTOSAR-style SWC** – model runnables and ports with traits and run them on a fixed-rate scheduler.
