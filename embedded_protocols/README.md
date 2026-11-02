# Embedded protocol stack — C core, C++17 drivers, Linux & MCU ports

Portable, allocation-free implementations of the serial, fieldbus and
time-sensitive Ethernet protocols used in automotive, industrial and
telematics ECUs:

| Area | What is implemented |
|---|---|
| **SPI** | Multi-device bus model with per-device mode/clock (reconfigures only on change), CS-held multi-segment transactions, register helpers, bit-banged master (modes 0–3) |
| **I2C / SMBus** | Message-list transfers with repeated START, register access (8/16-bit addresses), bus scan, SMBus byte/word/block with **PEC** (CRC-8), bit-banged master with clock stretching, arbitration-loss detection and 9-clock **bus recovery** |
| **UART** | Interrupt-driven driver (TXE/TC/RXNE model) on lock-free SPSC rings, error accounting, baud divisor + error calculation, character timing |
| **RS-232 / RS-422 / RS-485** | Physical-layer adapter: RS-232 modem lines (DTR/RTS/CTS/DSR/DCD/RI), RS-422 point-to-point or multidrop, RS-485 half/full duplex with **DE released on TC (last stop bit)**, pre/post turnaround delays, receiver gating or **echo compare / collision detection**, switchable termination; Modbus t1.5/t3.5 |
| **Framing** | COBS + CRC-16 packet framing with a self-resynchronising streaming decoder |
| **Modbus RTU** (C++) | Master (FC 01/03/04/05/06/16, exceptions, broadcast, CRC, t3.5 frame detection) and template slave over any register map |
| **CAN / CAN FD** | Frame model + validation, DLC↔length, **bit-timing calculator** (bxCAN, MCP2515, M_CAN limits, CiA sample points), acceptance filters, ISO 11898-1 error states, driver core with ISR RX queue and bus-off tracking |
| **ISO-TP** (ISO 15765-2) | SF/FF/CF/FC segmentation & reassembly, block size, STmin (ms and 100 µs ranges), FC.WAIT/OVFLW, N_Bs/N_Cr timeouts — UDS/OBD transport |
| **MCP2515** (C++) | SPI CAN controller driver: reset, CNF1-3 from the bit-timing calculator, modes, 3 TX buffers, 2 RX buffers, error counters; plugs into the C CAN core |
| **Ethernet** | Frame build/parse with **802.1Q** (PCP/DEI/VID), FCS, wire-time maths, IPv4/UDP with checksums, ARP, Clause-22 **PHY/MDIO** (reset, ID, autoneg result incl. 1000BASE-T), static EtherType dispatch |
| **PTP** (IEEE 1588-2019 / 802.1AS) | Message codec (Sync, Delay_Req, Follow_Up, Delay_Resp, Pdelay_*, Announce), E2E & P2P delay maths, neighbour rate ratio, **BMCA** dataset comparison, **PI servo** (unlocked → jump → locked, anti-windup), ordinary-clock slave with foreign-master qualification, two-step/one-step, delay filtering and announce-receipt timeout |
| **TSN** | **802.1Qbv** time-aware shaper (cycle extension/truncation, guard band, next TX opportunity, Linux `taprio` command generation), **802.1Qav** credit-based shaper, **802.1CB** FRER vector recovery (duplicate elimination, wrap-around, recovery timeout), deterministic egress scheduler (static frame pool, per-class FIFOs, strict priority + TAS + CBS + LaunchTime, next-event computation) |

## Layout

```
include/ep/        C API (one header per protocol)            -> MCU firmware
src/               C11 implementation, no heap, no OS
cpp/include/epp/   C++17 header-only layer (-fno-exceptions -fno-rtti friendly)
ports/linux/       spidev, i2c-dev, termios+TIOCSRS485, SocketCAN, AF_PACKET+SO_TIMESTAMPING/SO_TXTIME, PHC
ports/stm32f4/     register-level USART (IRQ), SPI, bxCAN backends (no vendor HAL)
examples/linux/    ptp_e2e_slave, isotp_socketcan, modbus_rs485, tsn_taprio_plan
tests/             C + C++ unit tests on simulated hardware (bit-level SPI slave,
                   bit-level I2C EEPROM, USART ISR model, MCP2515 register model,
                   closed-loop PTP grandmaster/slave simulation)
```

## Architecture

```
            application / UDS / Modbus map / control loop
  ┌────────────────────────────────────────────────────────────────┐
  │ C++17: Mcp2515<Spi>  Eeprom24xx<I2c>  RtuMaster<Port>  GCL<N>   │  templates, zero virtuals
  ├────────────────────────────────────────────────────────────────┤
  │ C core: spi  i2c  uart+serial_line  framing  can  isotp         │  pure logic, unit-tested
  │         eth  ptp(codec, BMCA, servo, OC)  tsn(TAS, CBS, FRER)   │  on the host
  ├────────────────────────────────────────────────────────────────┤
  │ ops tables:  ep_spi_bus_ops_t  ep_i2c_bus_ops_t  ep_uart_ops_t  │  the only hardware seam
  │              ep_can_ops_t  ep_mdio_t  ep_ptp_clock_ops_t        │
  ├───────────────────────┬────────────────────────┬───────────────┤
  │ ports/stm32f4 (regs)  │ ports/linux (ioctl)     │ tests/sim     │
  └───────────────────────┴────────────────────────┴───────────────┘
```

Every hardware dependency is an *ops* table (function pointers + context).
A new MCU needs only those few functions; the protocol logic is shared
between the MCU, embedded Linux and the host test build.

## Build and test (host)

```bash
cmake -S . -B build && cmake --build build -j && ctest --test-dir build --output-on-failure
cmake -S . -B build-asan -DEP_SANITIZE=ON -DCMAKE_BUILD_TYPE=Debug   # ASan + UBSan
```

Options: `EP_BUILD_TESTS`, `EP_BUILD_LINUX`, `EP_BUILD_EXAMPLES`,
`EP_BUILD_STM32_CHECK`, `EP_WERROR` (default ON), `EP_SANITIZE`.
Warnings: `-Wall -Wextra -Wshadow -Wconversion -Werror`.

### Cross-compiling for a Cortex-M

```bash
arm-none-eabi-gcc -mcpu=cortex-m4 -mthumb -mfloat-abi=hard -mfpu=fpv4-sp-d16 -Os \
  -Iinclude -Iports/stm32f4 -c src/*.c ports/stm32f4/*.c
```

Flash footprint for Cortex-M4 at `-Os` (clang 18): the whole core is about
22 KB of code with zero `.data`/`.bss`, because all storage comes from the
caller. SPI 1.5 KB, I2C+SMBus 2.6 KB, UART+RS-485 2.1 KB, CAN 1.5 KB,
ISO-TP 1.5 KB, Ethernet 2.3 KB, PTP 4.9 KB, TSN 4.0 KB.

## Porting to a new MCU (example: UART + RS-485)

```c
static const ep_uart_ops_t my_uart_ops = {
    .configure = my_configure,     /* program BRR / LCR from ep_uart_config_t      */
    .tx_kick = my_enable_txe_irq,  /* driver pulls bytes in the ISR                 */
    .critical_enter = my_mask_irq, /* used around the TX start/finish race          */
    .critical_exit = my_unmask_irq,
};

void UARTx_IRQHandler(void) {
    if (rx_ready)            ep_uart_isr_rx(&uart, DR, err_flags);
    if (txe && txe_enabled)  { if (ep_uart_isr_tx_next(&uart, &b)) DR = b; else enable_tc_instead_of_txe(); }
    if (tc && tc_enabled)    { disable_tc(); ep_uart_isr_tx_complete(&uart); }  /* DE released here */
}

ep_line_config_t lc;
ep_line_config_default(&lc, EP_LINE_RS485_HALF);
lc.gpio = board_gpio; lc.de_pin = PIN_DE; lc.re_n_pin = PIN_RE;
ep_serial_line_init(&rs485, &uart, &lc, &board_clock);
ep_serial_line_send(&rs485, frame, len);
```

`ports/stm32f4/` is a complete reference port (USART2, SPI1, CAN1).

## Linux examples

```bash
sudo ./build/ptp_e2e_slave eth0            # IEEE 1588 slave steering the NIC PHC (hw timestamps)
./build/isotp_socketcan vcan0              # UDS ReadDataByIdentifier(VIN) over ISO-TP
./build/modbus_rs485 /dev/ttyUSB0 9600 1 0 10
./build/tsn_taprio_plan eth0 1000          # taprio / cbs / etf commands + worst-case wait
```

`tsn_taprio_plan` prints, for example:

```
tc qdisc replace dev eth0 parent root handle 100 taprio num_tc 8 map 0 1 2 5 3 4 7 7 ... \
   base-time 1000000000 sched-entry S 80 250000 sched-entry S 7f 750000 cycle-time 1000000 clockid CLOCK_TAI
tc qdisc replace dev eth0 parent 100:6 cbs idleslope 20000 sendslope -980000 hicredit 243 locredit -11932 offload 1
```

## Design notes

* **RS-485 turnaround.** DE is dropped on *transmission complete* (TC: the
  last stop bit has left the shift register), not on TXE (data register
  empty), which would chop off the final byte. Data queued while TC is
  pending keeps the driver on: the test suite covers that race.
* **Deterministic TSN egress.** No allocation, a fixed frame pool with
  per-class limits, O(classes) dequeue decisions, and
  `ep_tsn_egress_next_event()` to program a one-shot timer instead of
  polling. A frame is released only if it *finishes* before its gate closes
  (guard band), so scheduled traffic never sees interference.
* **Compile-time schedules.** `epp::tsn::makeGcl({...})` is `constexpr`, so
  cycle time, per-class bandwidth and worst-case gate wait can be checked
  with `static_assert` as part of the build.
* **PTP servo.** A PI controller in the style of linuxptp. The first sample
  pair estimates frequency, a large initial offset is stepped once, and
  after that the clock is only slewed. Integration stops while the output
  is saturated (anti-windup). In the closed-loop test (1.5 ms initial
  offset, 50 ppm crystal error, 10 µs path) the servo learns the frequency
  to within 1 ppb and holds the error within ±1 ns.
* **Testing without hardware.** The I2C EEPROM and SPI slave simulators
  decode the protocol bit by bit from GPIO edges, so the bit-banged
  masters are tested against the wire protocol itself rather than a mock
  of their own API.
