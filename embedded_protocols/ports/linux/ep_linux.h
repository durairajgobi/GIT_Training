/*
 * ep_linux.h - Linux backends for the embedded protocol stack.
 *
 *   SPI       /dev/spidevB.C        (spidev, SPI_IOC_MESSAGE)
 *   I2C       /dev/i2c-N            (i2c-dev, I2C_RDWR)
 *   UART      /dev/ttyX             (termios; RS-485 via TIOCSRS485)
 *   CAN       SocketCAN (can0)      (CAN_RAW, CAN FD, hw timestamps)
 *   Ethernet  AF_PACKET raw socket  (SO_TIMESTAMPING hw tx/rx, SO_TXTIME)
 *   PTP       /dev/ptpN PHC         (clock_adjtime: frequency + step)
 *
 * Embedded Linux (i.MX8, AM62x, Raspberry Pi CM4, Jetson) reuses the
 * exact same protocol code that runs on the MCU; only these backends
 * differ.
 */
#ifndef EP_LINUX_H
#define EP_LINUX_H

#include <time.h>

#include "ep/ep_can.h"
#include "ep/ep_i2c.h"
#include "ep/ep_ptp.h"
#include "ep/ep_serial_line.h"
#include "ep/ep_spi.h"
#include "ep/ep_uart.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------ clock ------------------------------ */
const ep_clock_t *ep_linux_clock(void); /* CLOCK_MONOTONIC + nanosleep */

/* ------------------------------ SPI -------------------------------- */
typedef struct {
    int fd;
} ep_linux_spidev_t;

ep_status_t ep_linux_spidev_open(ep_linux_spidev_t *s, const char *path);
void ep_linux_spidev_close(ep_linux_spidev_t *s);
extern const ep_spi_bus_ops_t ep_linux_spidev_ops; /* kernel drives CS */

/* ------------------------------ I2C -------------------------------- */
typedef struct {
    int fd;
} ep_linux_i2cdev_t;

ep_status_t ep_linux_i2cdev_open(ep_linux_i2cdev_t *d, const char *path);
void ep_linux_i2cdev_close(ep_linux_i2cdev_t *d);
extern const ep_i2c_bus_ops_t ep_linux_i2cdev_ops;

/* ------------------------- UART / RS-232/422/485 ------------------- */
typedef struct {
    int fd;
} ep_linux_serial_t;

/* line == NULL or EP_LINE_RS232/RS422: plain UART. EP_LINE_RS485_*:
 * kernel RS-485 mode, RTS used as DE with the configured delays. */
ep_status_t ep_linux_serial_open(ep_linux_serial_t *s, const char *path,
                                 const ep_uart_config_t *cfg, const ep_line_config_t *line);
void ep_linux_serial_close(ep_linux_serial_t *s);
long ep_linux_serial_write(ep_linux_serial_t *s, const uint8_t *d, size_t n);
int ep_linux_serial_read_byte(ep_linux_serial_t *s, uint32_t timeout_us); /* -1 timeout */
void ep_linux_serial_drain(ep_linux_serial_t *s);
void ep_linux_serial_flush_input(ep_linux_serial_t *s);
ep_status_t ep_linux_serial_set_modem(ep_linux_serial_t *s, uint8_t mask, uint8_t value);
uint8_t ep_linux_serial_get_modem(ep_linux_serial_t *s);

/* ---------------------------- SocketCAN ---------------------------- */
typedef struct {
    int fd;
    bool fd_frames;
} ep_linux_can_t;

ep_status_t ep_linux_can_open(ep_linux_can_t *c, const char *ifname, bool canfd, bool timestamps);
void ep_linux_can_close(ep_linux_can_t *c);
ep_status_t ep_linux_can_send(ep_linux_can_t *c, const ep_can_frame_t *f);
ep_status_t ep_linux_can_recv(ep_linux_can_t *c, ep_can_frame_t *f, int timeout_ms);
ep_status_t ep_linux_can_set_filters(ep_linux_can_t *c, const ep_can_filter_t *f, size_t n);
extern const ep_can_ops_t ep_linux_can_ops; /* bit timing via `ip link`, not here */

/* -------------------- Raw Ethernet with timestamps ----------------- */
typedef struct {
    int fd;
    int ifindex;
    uint8_t mac[6];
    bool hw_ts;
} ep_linux_raw_t;

/* hw_timestamps: program the NIC (SIOCSHWTSTAMP) and request hardware
 * tx/rx timestamps; falls back to software stamps if the NIC refuses. */
ep_status_t ep_linux_raw_open(ep_linux_raw_t *r, const char *ifname, uint16_t ethertype,
                              bool hw_timestamps);
void ep_linux_raw_close(ep_linux_raw_t *r);
ep_status_t ep_linux_raw_join(ep_linux_raw_t *r, const uint8_t mac[6]);
/* tx_ts (optional): egress timestamp read back from the error queue. */
ep_status_t ep_linux_raw_send(ep_linux_raw_t *r, const uint8_t *frame, size_t len, int64_t *tx_ts);
ep_status_t ep_linux_raw_recv(ep_linux_raw_t *r, uint8_t *buf, size_t cap, size_t *len,
                              int64_t *rx_ts, int timeout_ms);
/* LaunchTime (ETF / taprio txtime-assist): frame leaves at txtime_ns on clk. */
ep_status_t ep_linux_raw_enable_txtime(ep_linux_raw_t *r, clockid_t clk, bool deadline_mode);
ep_status_t ep_linux_raw_send_at(ep_linux_raw_t *r, const uint8_t *frame, size_t len,
                                 uint64_t txtime_ns, int priority);

/* ------------------------- PTP hardware clock ---------------------- */
typedef struct {
    int fd;
    clockid_t clk;
    double max_ppb;
} ep_linux_phc_t;

ep_status_t ep_linux_phc_open(ep_linux_phc_t *p, const char *path);       /* "/dev/ptp0" */
ep_status_t ep_linux_phc_open_iface(ep_linux_phc_t *p, const char *ifname); /* via ethtool */
void ep_linux_phc_close(ep_linux_phc_t *p);
ep_ptp_clock_ops_t ep_linux_phc_clock_ops(ep_linux_phc_t *p);

#ifdef __cplusplus
}
#endif

#endif /* EP_LINUX_H */
