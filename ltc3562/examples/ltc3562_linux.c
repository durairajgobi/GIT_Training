/*
 * ltc3562_linux.c - Command-line tool driving an LTC3562 through Linux
 *                   i2c-dev (/dev/i2c-N).
 *
 * Usage:
 *   ltc3562_linux <bus> off
 *   ltc3562_linux <bus> <R600A|R400A|R600B|R400B> <mV> [mode] [Rtop Rbot]
 *
 *   mode : pulse | ldo | burst | forced   (default: pulse)
 *   Rtop/Rbot : feedback divider in ohms, required for R600A / R400A
 *
 * Example: ltc3562_linux 1 R600B 1800 burst
 *          ltc3562_linux 1 R600A 3300 pulse 309000 100000
 */
#include <errno.h>
#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "ltc3562.h"

struct linux_bus {
    int fd;
};

static int linux_i2c_write(void *user, uint8_t addr, const uint8_t *buf,
                           size_t len)
{
    struct linux_bus *bus = user;

    if (ioctl(bus->fd, I2C_SLAVE, addr) < 0)
        return -errno;
    return write(bus->fd, buf, len) == (ssize_t)len ? 0 : -EIO;
}

static int parse_reg(const char *s, ltc3562_reg_t *reg)
{
    int i;

    for (i = 0; i < LTC3562_NUM_REGULATORS; i++) {
        if (strcasecmp(s, ltc3562_reg_name((ltc3562_reg_t)i)) == 0) {
            *reg = (ltc3562_reg_t)i;
            return 0;
        }
    }
    return -1;
}

static int parse_mode(const char *s, ltc3562_mode_t *mode)
{
    static const struct { const char *name; ltc3562_mode_t mode; } map[] = {
        { "pulse",  LTC3562_MODE_PULSE_SKIP },
        { "ldo",    LTC3562_MODE_LDO },
        { "burst",  LTC3562_MODE_BURST },
        { "forced", LTC3562_MODE_FORCED_BURST },
    };
    size_t i;

    for (i = 0; i < sizeof(map) / sizeof(map[0]); i++) {
        if (strcasecmp(s, map[i].name) == 0) {
            *mode = map[i].mode;
            return 0;
        }
    }
    return -1;
}

static void usage(const char *prog)
{
    fprintf(stderr,
            "usage: %s <bus> off\n"
            "       %s <bus> <R600A|R400A|R600B|R400B> <mV> "
            "[pulse|ldo|burst|forced] [Rtop Rbot]\n", prog, prog);
}

int main(int argc, char **argv)
{
    char path[32];
    struct linux_bus bus;
    ltc3562_dev_t dev;
    ltc3562_reg_t reg;
    ltc3562_mode_t mode = LTC3562_MODE_PULSE_SKIP;
    uint32_t actual = 0;
    int ret;

    if (argc < 3) {
        usage(argv[0]);
        return 2;
    }

    snprintf(path, sizeof(path), "/dev/i2c-%s", argv[1]);
    bus.fd = open(path, O_RDWR);
    if (bus.fd < 0) {
        perror(path);
        return 1;
    }

    /* Note: init broadcasts a disable to all four rails. */
    ret = ltc3562_init(&dev, linux_i2c_write, &bus, 0);
    if (ret != LTC3562_OK) {
        fprintf(stderr, "init failed (%d)\n", ret);
        goto out;
    }

    if (strcmp(argv[2], "off") == 0) {
        printf("all regulators off\n");
        goto out;
    }

    if (argc < 4 || parse_reg(argv[2], &reg) ||
        (argc >= 5 && parse_mode(argv[4], &mode))) {
        usage(argv[0]);
        ret = 2;
        goto out;
    }

    if (ltc3562_is_type_a(reg)) {
        if (argc < 7) {
            fprintf(stderr, "%s needs Rtop and Rbot\n", argv[2]);
            ret = 2;
            goto out;
        }
        ltc3562_set_divider(&dev, reg, strtoul(argv[5], NULL, 0),
                            strtoul(argv[6], NULL, 0));
    }

    ret = ltc3562_configure(&dev, reg, strtoul(argv[3], NULL, 0), mode, true,
                            &actual);
    if (ret == LTC3562_OK)
        printf("%s enabled at %u mV\n", ltc3562_reg_name(reg), actual);
    else
        fprintf(stderr, "configure failed (%d)\n", ret);

out:
    close(bus.fd);
    return ret ? 1 : 0;
}
