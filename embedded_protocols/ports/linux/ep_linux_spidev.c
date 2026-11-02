#include "ep_linux.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/spi/spidev.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define MAX_SEGS 16

ep_status_t ep_linux_spidev_open(ep_linux_spidev_t *s, const char *path)
{
    s->fd = open(path, O_RDWR | O_CLOEXEC);
    return s->fd < 0 ? EP_ERR_IO : EP_OK;
}

void ep_linux_spidev_close(ep_linux_spidev_t *s)
{
    if (s->fd >= 0)
        close(s->fd);
    s->fd = -1;
}

static ep_status_t sd_configure(void *ctx, const ep_spi_config_t *cfg)
{
    ep_linux_spidev_t *s = ctx;
    uint32_t mode = (uint32_t)cfg->mode;
    if (cfg->lsb_first)
        mode |= SPI_LSB_FIRST;
    if (cfg->cs_active_high)
        mode |= SPI_CS_HIGH;
    uint8_t bits = cfg->bits_per_word;
    uint32_t hz = cfg->max_hz;
    if (ioctl(s->fd, SPI_IOC_WR_MODE32, &mode) < 0 ||
        ioctl(s->fd, SPI_IOC_WR_BITS_PER_WORD, &bits) < 0 ||
        ioctl(s->fd, SPI_IOC_WR_MAX_SPEED_HZ, &hz) < 0)
        return errno == EINVAL ? EP_ERR_UNSUPPORTED : EP_ERR_IO;
    return EP_OK;
}

/* One SPI_IOC_MESSAGE = one CS window; cs_change toggles CS in between. */
static ep_status_t sd_segments(void *ctx, uint8_t cs, const ep_spi_segment_t *segs, size_t n)
{
    (void)cs; /* the device node already selects the chip-select line */
    ep_linux_spidev_t *s = ctx;
    struct spi_ioc_transfer x[MAX_SEGS];
    if (n == 0)
        return EP_OK;
    if (n > MAX_SEGS)
        return EP_ERR_PARAM;
    memset(x, 0, sizeof(x));
    for (size_t i = 0; i < n; i++) {
        x[i].tx_buf = (uintptr_t)segs[i].tx;
        x[i].rx_buf = (uintptr_t)segs[i].rx;
        x[i].len = (uint32_t)segs[i].len;
        x[i].delay_usecs = segs[i].delay_us;
        x[i].cs_change = (segs[i].cs_change && i + 1 < n) ? 1 : 0;
    }
    return ioctl(s->fd, SPI_IOC_MESSAGE((unsigned)n), x) < 0 ? EP_ERR_IO : EP_OK;
}

static ep_status_t sd_transfer(void *ctx, const uint8_t *tx, uint8_t *rx, size_t len)
{
    ep_spi_segment_t seg = {.tx = tx, .rx = rx, .len = len};
    return sd_segments(ctx, 0, &seg, 1);
}

const ep_spi_bus_ops_t ep_linux_spidev_ops = {
    .configure = sd_configure,
    .transfer = sd_transfer,
    .chip_select = NULL,
    .transfer_segments = sd_segments,
};
