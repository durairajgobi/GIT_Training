// SPDX-License-Identifier: GPL-2.0
/* Test for 027_mmap_page: map the kernel page, print it, write into it. */
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>

int main(int argc, char **argv)
{
	const char *dev = argc > 1 ? argv[1] : "/dev/mmap_page";
	int fd = open(dev, O_RDWR);
	char *p;

	if (fd < 0) {
		perror("open");
		return 1;
	}
	p = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (p == MAP_FAILED) {
		perror("mmap");
		return 1;
	}
	printf("kernel says: %s", p);
	strcpy(p, "user space was here\n");	/* visible in dmesg on rmmod */
	munmap(p, 4096);
	close(fd);
	return 0;
}
