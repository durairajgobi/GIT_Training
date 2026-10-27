// SPDX-License-Identifier: GPL-2.0
/* Test for 022_ioctl_struct: read, modify and write a config struct. */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>

struct dev_config {
	uint32_t baud_rate;
	uint8_t  data_bits;
	uint8_t  parity;
	uint8_t  stop_bits;
	char     label[16];
};

#define CFG_MAGIC 'C'
#define CFG_GET   _IOR(CFG_MAGIC, 1, struct dev_config)
#define CFG_SET   _IOW(CFG_MAGIC, 2, struct dev_config)

int main(void)
{
	struct dev_config c;
	int fd = open("/dev/ioctl_struct", O_RDWR);

	if (fd < 0) {
		perror("open /dev/ioctl_struct");
		return 1;
	}
	if (ioctl(fd, CFG_GET, &c) < 0) {
		perror("CFG_GET");
		return 1;
	}
	printf("current: %s %u baud %u data bits\n", c.label, c.baud_rate, c.data_bits);

	c.baud_rate = 9600;
	c.data_bits = 7;
	c.parity = 2;
	strncpy(c.label, "console", sizeof(c.label) - 1);
	if (ioctl(fd, CFG_SET, &c) < 0)
		perror("CFG_SET");

	c.data_bits = 9;	/* invalid, expect EINVAL */
	if (ioctl(fd, CFG_SET, &c) < 0)
		perror("CFG_SET with data_bits=9 (expected failure)");
	close(fd);
	return 0;
}
