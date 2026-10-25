// SPDX-License-Identifier: MIT
#include "tty.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/timerfd.h>
#include <termios.h>
#include <unistd.h>

static speed_t baud_to_speed(int baud)
{
	switch (baud) {
	case 9600: return B9600;
	case 57600: return B57600;
	case 230400: return B230400;
	case 460800: return B460800;
	case 921600: return B921600;
	default: return B115200;
	}
}

int tty_open_raw(const char *path, int baud)
{
	struct termios t;
	int fd = open(path, O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);

	if (fd < 0)
		return -1;
	if (tcgetattr(fd, &t) == 0) {
		cfmakeraw(&t);
		cfsetispeed(&t, baud_to_speed(baud));
		cfsetospeed(&t, baud_to_speed(baud));
		t.c_cflag |= CLOCAL | CREAD;
		t.c_cflag &= ~CRTSCTS;
		tcsetattr(fd, TCSANOW, &t);
		tcflush(fd, TCIOFLUSH);
	}
	/* USB CDC-ACM ports fail tcgetattr on some modems; raw I/O still works. */
	return fd;
}

int tty_write_cmd(int fd, const char *cmd)
{
	size_t len = strlen(cmd);
	char buf[512];

	if (len + 1 > sizeof(buf))
		return -1;
	memcpy(buf, cmd, len);
	buf[len++] = '\r';

	for (size_t off = 0; off < len;) {
		ssize_t n = write(fd, buf + off, len - off);

		if (n < 0) {
			if (errno == EINTR || errno == EAGAIN)
				continue;
			return -1;
		}
		off += (size_t)n;
	}
	return 0;
}

int timerfd_arm_ms(int tfd, unsigned int ms)
{
	struct itimerspec its = { 0 };

	its.it_value.tv_sec = ms / 1000;
	its.it_value.tv_nsec = (long)(ms % 1000) * 1000000L;
	return timerfd_settime(tfd, 0, &its, NULL);
}
