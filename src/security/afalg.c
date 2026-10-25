// SPDX-License-Identifier: MIT
#include "afalg.h"

#include <errno.h>
#include <linux/if_alg.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#ifndef SOL_ALG
#define SOL_ALG 279
#endif

static int open_op(const char *alg, const void *key, size_t keylen)
{
	struct sockaddr_alg sa = { .salg_family = AF_ALG };
	int tfm, op;

	if (strlen(alg) >= sizeof(sa.salg_name)) {
		errno = EINVAL;
		return -1;
	}
	strcpy((char *)sa.salg_type, "hash");
	strcpy((char *)sa.salg_name, alg);

	tfm = socket(AF_ALG, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
	if (tfm < 0)
		return -1;
	if (bind(tfm, (struct sockaddr *)&sa, sizeof(sa)) < 0 ||
	    (key && setsockopt(tfm, SOL_ALG, ALG_SET_KEY, key, (socklen_t)keylen) < 0)) {
		int e = errno;

		close(tfm);
		errno = e;
		return -1;
	}
	op = accept4(tfm, NULL, NULL, SOCK_CLOEXEC);
	close(tfm); /* the op socket keeps the transform alive */
	return op;
}

static int finish(int op, uint8_t *out, size_t outlen)
{
	ssize_t n = read(op, out, outlen);
	int e = errno;

	close(op);
	errno = e;
	return n < 0 ? -1 : (int)n;
}

int afalg_hash(const char *alg, const void *key, size_t keylen,
	       const void *data, size_t len, uint8_t *out, size_t outlen)
{
	int op = open_op(alg, key, keylen);

	if (op < 0)
		return -1;
	if (len && send(op, data, len, 0) != (ssize_t)len) {
		close(op);
		return -1;
	}
	return finish(op, out, outlen);
}

int afalg_hash_fd(const char *alg, int fd, uint8_t *out, size_t outlen)
{
	uint8_t buf[64 * 1024];
	int op = open_op(alg, NULL, 0);
	ssize_t n;

	if (op < 0)
		return -1;
	while ((n = read(fd, buf, sizeof(buf))) > 0) {
		/* MSG_MORE keeps the hash open across chunks. */
		if (send(op, buf, (size_t)n, MSG_MORE) != n) {
			close(op);
			return -1;
		}
	}
	if (n < 0) {
		close(op);
		return -1;
	}
	return finish(op, out, outlen);
}
