#include "ep_linux.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/serial.h>
#include <poll.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

static speed_t to_speed(uint32_t baud)
{
    switch (baud) {
    case 1200: return B1200;
    case 2400: return B2400;
    case 4800: return B4800;
    case 9600: return B9600;
    case 19200: return B19200;
    case 38400: return B38400;
    case 57600: return B57600;
    case 115200: return B115200;
    case 230400: return B230400;
    case 460800: return B460800;
    case 500000: return B500000;
    case 921600: return B921600;
    case 1000000: return B1000000;
    case 2000000: return B2000000;
    case 3000000: return B3000000;
    default: return 0;
    }
}

static ep_status_t apply_termios(int fd, const ep_uart_config_t *cfg)
{
    struct termios t;
    speed_t sp = to_speed(cfg->baud);
    if (!sp)
        return EP_ERR_UNSUPPORTED;
    if (tcgetattr(fd, &t) < 0)
        return EP_ERR_IO;
    cfmakeraw(&t);
    cfsetispeed(&t, sp);
    cfsetospeed(&t, sp);
    t.c_cflag |= CLOCAL | CREAD;
    t.c_cflag &= (tcflag_t)~CSIZE;
    switch (cfg->data_bits) {
    case 5: t.c_cflag |= CS5; break;
    case 6: t.c_cflag |= CS6; break;
    case 7: t.c_cflag |= CS7; break;
    case 8: t.c_cflag |= CS8; break;
    default: return EP_ERR_UNSUPPORTED;
    }
    t.c_cflag &= (tcflag_t)~(PARENB | PARODD);
    if (cfg->parity == EP_PARITY_EVEN)
        t.c_cflag |= PARENB;
    else if (cfg->parity == EP_PARITY_ODD)
        t.c_cflag |= PARENB | PARODD;
    if (cfg->stop_bits == 2)
        t.c_cflag |= CSTOPB;
    else
        t.c_cflag &= (tcflag_t)~CSTOPB;
    t.c_cflag &= (tcflag_t)~CRTSCTS;
    t.c_iflag &= (tcflag_t)~(IXON | IXOFF | IXANY);
    if (cfg->flow == EP_FLOW_RTSCTS)
        t.c_cflag |= CRTSCTS;
    else if (cfg->flow == EP_FLOW_XONXOFF)
        t.c_iflag |= IXON | IXOFF;
    if (cfg->parity != EP_PARITY_NONE)
        t.c_iflag |= INPCK; /* drop bytes with parity errors */
    t.c_cc[VMIN] = 0;
    t.c_cc[VTIME] = 0;
    return tcsetattr(fd, TCSANOW, &t) < 0 ? EP_ERR_IO : EP_OK;
}

static ep_status_t apply_rs485(int fd, const ep_line_config_t *line)
{
    struct serial_rs485 rs;
    memset(&rs, 0, sizeof(rs));
    if (line && (line->mode == EP_LINE_RS485_HALF || line->mode == EP_LINE_RS485_FULL)) {
        rs.flags = SER_RS485_ENABLED;
        /* RTS drives DE: level while sending / after sending */
        rs.flags |= line->de_active_high ? SER_RS485_RTS_ON_SEND : SER_RS485_RTS_AFTER_SEND;
        if (line->mode == EP_LINE_RS485_FULL || line->echo_check)
            rs.flags |= SER_RS485_RX_DURING_TX;
        rs.delay_rts_before_send = (line->pre_delay_us + 999) / 1000; /* ms */
        rs.delay_rts_after_send = (line->post_delay_us + 999) / 1000;
        if (ioctl(fd, TIOCSRS485, &rs) < 0)
            return EP_ERR_UNSUPPORTED; /* UART driver lacks RS-485 support */
    }
    return EP_OK;
}

ep_status_t ep_linux_serial_open(ep_linux_serial_t *s, const char *path,
                                 const ep_uart_config_t *cfg, const ep_line_config_t *line)
{
    s->fd = open(path, O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
    if (s->fd < 0)
        return EP_ERR_IO;
    ep_status_t st = apply_termios(s->fd, cfg);
    if (st == EP_OK)
        st = apply_rs485(s->fd, line);
    if (st != EP_OK) {
        close(s->fd);
        s->fd = -1;
        return st;
    }
    tcflush(s->fd, TCIOFLUSH);
    return EP_OK;
}

void ep_linux_serial_close(ep_linux_serial_t *s)
{
    if (s->fd >= 0)
        close(s->fd);
    s->fd = -1;
}

long ep_linux_serial_write(ep_linux_serial_t *s, const uint8_t *d, size_t n)
{
    size_t off = 0;
    while (off < n) {
        ssize_t w = write(s->fd, d + off, n - off);
        if (w < 0) {
            if (errno == EINTR)
                continue;
            if (errno == EAGAIN) {
                struct pollfd p = {.fd = s->fd, .events = POLLOUT};
                poll(&p, 1, 100);
                continue;
            }
            return -1;
        }
        off += (size_t)w;
    }
    return (long)off;
}

int ep_linux_serial_read_byte(ep_linux_serial_t *s, uint32_t timeout_us)
{
    struct pollfd p = {.fd = s->fd, .events = POLLIN};
    /* poll() has ms resolution: round up so short t3.5 values still wait */
    int ms = (int)((timeout_us + 999) / 1000);
    for (;;) {
        int r = poll(&p, 1, ms);
        if (r < 0 && errno == EINTR)
            continue;
        if (r <= 0)
            return -1;
        uint8_t b;
        ssize_t n = read(s->fd, &b, 1);
        if (n == 1)
            return b;
        if (n < 0 && (errno == EAGAIN || errno == EINTR))
            continue;
        return -1;
    }
}

void ep_linux_serial_drain(ep_linux_serial_t *s)
{
    tcdrain(s->fd);
}

void ep_linux_serial_flush_input(ep_linux_serial_t *s)
{
    tcflush(s->fd, TCIFLUSH);
}

ep_status_t ep_linux_serial_set_modem(ep_linux_serial_t *s, uint8_t mask, uint8_t value)
{
    int set = 0, clr = 0;
    if (mask & EP_MODEM_DTR) { if (value & EP_MODEM_DTR) set |= TIOCM_DTR; else clr |= TIOCM_DTR; }
    if (mask & EP_MODEM_RTS) { if (value & EP_MODEM_RTS) set |= TIOCM_RTS; else clr |= TIOCM_RTS; }
    if ((set && ioctl(s->fd, TIOCMBIS, &set) < 0) || (clr && ioctl(s->fd, TIOCMBIC, &clr) < 0))
        return EP_ERR_IO;
    return EP_OK;
}

uint8_t ep_linux_serial_get_modem(ep_linux_serial_t *s)
{
    int m = 0;
    uint8_t v = 0;
    if (ioctl(s->fd, TIOCMGET, &m) < 0)
        return 0;
    if (m & TIOCM_CTS) v |= EP_MODEM_CTS;
    if (m & TIOCM_DSR) v |= EP_MODEM_DSR;
    if (m & TIOCM_CD) v |= EP_MODEM_DCD;
    if (m & TIOCM_RI) v |= EP_MODEM_RI;
    if (m & TIOCM_DTR) v |= EP_MODEM_DTR;
    if (m & TIOCM_RTS) v |= EP_MODEM_RTS;
    return v;
}
