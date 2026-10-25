/* SPDX-License-Identifier: MIT */
#ifndef TCU_AFALG_H
#define TCU_AFALG_H

#include <stddef.h>
#include <stdint.h>

/*
 * Linux kernel crypto API from user space (AF_ALG, CONFIG_CRYPTO_USER_API_HASH).
 *
 * On i.MX8 the kernel picks the highest-priority driver for an algorithm,
 * so "sha256" resolves to the CAAM implementation ("sha256-caam") when the
 * CAAM job ring is up; "hmac(sha256)" likewise. /proc/crypto shows which
 * driver is bound. Large firmware-image hashing is offloaded this way.
 */

/* One-shot digest. Returns digest length or -1 (errno set). */
int afalg_hash(const char *alg, const void *key, size_t keylen,
	       const void *data, size_t len, uint8_t *out, size_t outlen);

/* Streamed digest of a file descriptor (e.g. an OTA image). */
int afalg_hash_fd(const char *alg, int fd, uint8_t *out, size_t outlen);

#endif
