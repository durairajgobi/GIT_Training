// SPDX-License-Identifier: MIT
/* Compares AF_ALG against known vectors; skips if AF_ALG is unavailable
 * (containers commonly block the socket family). */
#include <errno.h>
#include <stdio.h>
#include <sys/socket.h>
#include <unistd.h>

#include "afalg.h"
#include "check.h"

static void hex(const uint8_t *b, int n, char *out)
{
	for (int i = 0; i < n; i++)
		sprintf(out + 2 * i, "%02x", b[i]);
}

int main(void)
{
	uint8_t md[32];
	char h[65];
	int fd, n;

	fd = socket(AF_ALG, SOCK_SEQPACKET, 0);
	if (fd < 0) {
		printf("AF_ALG unavailable (%d), skipping\n", errno);
		return 0;
	}
	close(fd);

	n = afalg_hash("sha256", NULL, 0, "abc", 3, md, sizeof(md));
	CHECK_EQ(n, 32);
	hex(md, 32, h);
	CHECK_STR(h, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");

	/* RFC 4231 test case 2 */
	n = afalg_hash("hmac(sha256)", "Jefe", 4, "what do ya want for nothing?", 28, md, sizeof(md));
	CHECK_EQ(n, 32);
	hex(md, 32, h);
	CHECK_STR(h, "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843");

	CHECK_EQ(afalg_hash("no-such-alg", NULL, 0, "x", 1, md, sizeof(md)), -1);
	CHECK_DONE();
}
