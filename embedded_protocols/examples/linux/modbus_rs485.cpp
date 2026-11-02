// modbus_rs485 - Modbus RTU master on a Linux RS-485 port.
//
//   ./modbus_rs485 /dev/ttyUSB0 9600 1 0 10     # slave 1, read 10 holding regs @0
//
// The kernel toggles DE through RTS (TIOCSRS485), so this works with
// USB-RS485 dongles, i.MX/AM335x UARTs with RS-485 transceivers, etc.
#include "epp/modbus_rtu.hpp"
#include "ep/ep_serial_line.h"
#include "ep_linux.h"

#include <cstdio>
#include <cstdlib>

class LinuxSerialPort {
public:
    explicit LinuxSerialPort(ep_linux_serial_t &s) : s_(s) {}
    void flushInput() { ep_linux_serial_flush_input(&s_); }
    std::size_t write(const std::uint8_t *d, std::size_t n)
    {
        long w = ep_linux_serial_write(&s_, d, n);
        return w < 0 ? 0 : static_cast<std::size_t>(w);
    }
    void drain() { ep_linux_serial_drain(&s_); }
    int readByte(std::uint32_t timeout_us) { return ep_linux_serial_read_byte(&s_, timeout_us); }

private:
    ep_linux_serial_t &s_;
};

int main(int argc, char **argv)
{
    if (argc < 6) {
        std::fprintf(stderr, "usage: %s <tty> <baud> <slave> <addr> <count>\n", argv[0]);
        return 2;
    }
    ep_uart_config_t cfg = EP_UART_CONFIG_8N1(static_cast<std::uint32_t>(std::atoi(argv[2])));
    cfg.parity = EP_PARITY_EVEN; // Modbus default: 8E1
    ep_line_config_t line;
    ep_line_config_default(&line, EP_LINE_RS485_HALF);

    ep_linux_serial_t tty;
    ep_status_t st = ep_linux_serial_open(&tty, argv[1], &cfg, &line);
    if (st == EP_ERR_UNSUPPORTED) {
        // e.g. FTDI/CH340 dongles with automatic direction control
        std::fprintf(stderr, "note: kernel RS-485 mode unavailable, using plain UART\n");
        st = ep_linux_serial_open(&tty, argv[1], &cfg, nullptr);
    }
    if (st != EP_OK) {
        std::perror(argv[1]);
        return 1;
    }

    LinuxSerialPort port(tty);
    epp::modbus::RtuMaster<LinuxSerialPort> master(port, ep_line_t35_us(&cfg), 500000);

    auto slave = static_cast<std::uint8_t>(std::atoi(argv[3]));
    auto addr = static_cast<std::uint16_t>(std::atoi(argv[4]));
    auto count = static_cast<std::size_t>(std::atoi(argv[5]));
    if (count == 0 || count > epp::modbus::kMaxReadRegs)
        return 2;
    std::uint16_t regs[epp::modbus::kMaxReadRegs];
    epp::Status r = master.readHoldingRegisters(slave, addr, epp::Span<std::uint16_t>(regs, count));
    if (r != epp::Status::Ok) {
        std::fprintf(stderr, "request failed: %s (exception %u)\n", epp::toString(r),
                     static_cast<unsigned>(master.lastException()));
        return 1;
    }
    for (std::size_t i = 0; i < count; i++)
        std::printf("%5u: %5u (0x%04X)\n", static_cast<unsigned>(addr + i), regs[i], regs[i]);
    ep_linux_serial_close(&tty);
    return 0;
}
