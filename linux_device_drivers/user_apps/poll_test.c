// SPDX-License-Identifier: GPL-2.0
/* Test for 024_poll_select: wait up to 10 s for data with poll(). */
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>

int main(void)
{
	struct pollfd pfd;
	char buf[256];
	ssize_t n;
	int ret;

	pfd.fd = open("/dev/polldev", O_RDWR | O_NONBLOCK);
	if (pfd.fd < 0) {
		perror("open /dev/polldev");
		return 1;
	}
	pfd.events = POLLIN;
	printf("waiting... (run: echo hi > /dev/polldev)\n");
	ret = poll(&pfd, 1, 10000);
	if (ret == 0) {
		printf("timeout\n");
	} else if (ret > 0 && (pfd.revents & POLLIN)) {
		n = read(pfd.fd, buf, sizeof(buf) - 1);
		if (n > 0) {
			buf[n] = '\0';
			printf("got: %s", buf);
		}
	} else {
		perror("poll");
	}
	close(pfd.fd);
	return 0;
}
