// SPDX-License-Identifier: MIT
/*
 * tcu-hash - hash files through the kernel crypto API.
 *
 *   tcu-hash [-a sha256] <file>...
 *
 * Used by the OTA agent so image verification runs on CAAM rather than
 * the Cortex-A cores while the unit is also routing hotspot traffic.
 */
#include <fcntl.h>
#include <getopt.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "afalg.h"

int main(int argc, char **argv)
{
	const char *alg = "sha256";
	uint8_t md[64];
	int opt, rc = 0;

	while ((opt = getopt(argc, argv, "a:")) != -1) {
		if (opt == 'a')
			alg = optarg;
		else
			return 2;
	}
	for (int i = optind; i < argc; i++) {
		int fd = open(argv[i], O_RDONLY | O_CLOEXEC);
		int n = fd < 0 ? -1 : afalg_hash_fd(alg, fd, md, sizeof(md));

		if (fd >= 0)
			close(fd);
		if (n < 0) {
			perror(argv[i]);
			rc = 1;
			continue;
		}
		for (int j = 0; j < n; j++)
			printf("%02x", md[j]);
		printf("  %s\n", argv[i]);
	}
	return rc;
}
