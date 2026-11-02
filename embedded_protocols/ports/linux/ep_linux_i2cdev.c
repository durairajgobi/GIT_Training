#include "ep_linux.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define MAX_MSGS 8

ep_status_t ep_linux_i2cdev_open(ep_linux_i2cdev_t *d, const char *path)
{
    d->fd = open(path, O_RDWR | O_CLOEXEC);
    return d->fd < 0 ? EP_ERR_IO : EP_OK;
}

void ep_linux_i2cdev_close(ep_linux_i2cdev_t *d)
{
    if (d->fd >= 0)
        close(d->fd);
    d->fd = -1;
}

static ep_status_t errno_to_status(int e)
{
    switch (e) {
    case ENXIO:
    case EREMOTEIO: return EP_ERR_NACK; /* adapter reports NACK */
    case ETIMEDOUT: return EP_ERR_TIMEOUT;
    case EAGAIN: return EP_ERR_ARBITRATION;
    case EOPNOTSUPP: return EP_ERR_UNSUPPORTED;
    default: return EP_ERR_IO;
    }
}

static ep_status_t id_xfer(void *ctx, ep_i2c_msg_t *msgs, size_t n)
{
    ep_linux_i2cdev_t *d = ctx;
    struct i2c_msg km[MAX_MSGS];
    if (n == 0 || n > MAX_MSGS)
        return EP_ERR_PARAM;
    for (size_t i = 0; i < n; i++) {
        km[i].addr = msgs[i].addr;
        km[i].flags = 0;
        if (msgs[i].flags & EP_I2C_M_RD) km[i].flags |= I2C_M_RD;
        if (msgs[i].flags & EP_I2C_M_TEN) km[i].flags |= I2C_M_TEN;
        if (msgs[i].flags & EP_I2C_M_NOSTART) km[i].flags |= I2C_M_NOSTART;
        km[i].len = msgs[i].len;
        km[i].buf = msgs[i].buf;
    }
    struct i2c_rdwr_ioctl_data data = {.msgs = km, .nmsgs = (uint32_t)n};
    if (ioctl(d->fd, I2C_RDWR, &data) < 0)
        return errno_to_status(errno);
    return EP_OK;
}

const ep_i2c_bus_ops_t ep_linux_i2cdev_ops = {
    .xfer = id_xfer,
    .recover = NULL, /* most adapters recover in the kernel driver */
};
