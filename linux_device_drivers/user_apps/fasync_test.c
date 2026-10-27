// SPDX-License-Identifier: GPL-2.0
/* Test for 025_fasync_sigio: receive SIGIO when the device is written. */
#define _GNU_SOURCE
#include <stdio.h>
#include <signal.h>
#include <fcntl.h>
#include <unistd.h>

static volatile sig_atomic_t got;

static void on_sigio(int sig)
{
	(void)sig;
	got++;
}

int main(void)
{
	int fd = open("/dev/asyncdev", O_RDWR);

	if (fd < 0) {
		perror("open /dev/asyncdev");
		return 1;
	}
	signal(SIGIO, on_sigio);
	fcntl(fd, F_SETOWN, getpid());
	fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | FASYNC);

	printf("pid %d waiting for SIGIO (run: echo x > /dev/asyncdev)\n", getpid());
	while (got < 3) {
		pause();
		printf("SIGIO #%d received\n", (int)got);
	}
	close(fd);
	return 0;
}
