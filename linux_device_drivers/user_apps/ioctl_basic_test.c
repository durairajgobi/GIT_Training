// SPDX-License-Identifier: GPL-2.0
/* Test for 021_ioctl_basic: set, get and reset a value via ioctl. */
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>

#define IOC_MAGIC   'T'
#define IOC_SET_VAL _IOW(IOC_MAGIC, 1, int)
#define IOC_GET_VAL _IOR(IOC_MAGIC, 2, int)
#define IOC_RESET   _IO(IOC_MAGIC, 3)

int main(void)
{
	int fd = open("/dev/ioctl_basic", O_RDWR);
	int v = 1234;

	if (fd < 0) {
		perror("open /dev/ioctl_basic");
		return 1;
	}
	if (ioctl(fd, IOC_SET_VAL, &v) < 0)
		perror("IOC_SET_VAL");
	v = 0;
	if (ioctl(fd, IOC_GET_VAL, &v) < 0)
		perror("IOC_GET_VAL");
	printf("value after set: %d\n", v);
	ioctl(fd, IOC_RESET);
	ioctl(fd, IOC_GET_VAL, &v);
	printf("value after reset: %d\n", v);
	close(fd);
	return 0;
}
