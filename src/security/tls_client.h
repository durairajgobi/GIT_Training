/* SPDX-License-Identifier: MIT */
#ifndef TCU_TLS_CLIENT_H
#define TCU_TLS_CLIENT_H

#include <stdbool.h>
#include <openssl/ssl.h>

/*
 * Mutual-TLS client used for all TCU -> backend channels.
 *
 *  - TLS 1.2 minimum, AEAD-only cipher suites
 *  - server name + certificate verified against the fleet root CA
 *  - device identity: per-unit client certificate issued at end-of-line
 *  - OCSP stapling required (status_request); a revoked or missing
 *    staple fails the handshake when require_ocsp is set
 */
struct tcu_tls_cfg {
	const char *host;
	const char *port;
	const char *ca_file;
	const char *cert_file;
	const char *key_file;  /* or an OSSL provider URI for TEE-held keys */
	bool require_ocsp;
	int timeout_s;
};

struct tcu_tls {
	SSL_CTX *ctx;
	SSL *ssl;
	int fd;
};

/* Returns 0 on success; on failure logs the OpenSSL error queue. */
int tcu_tls_connect(const struct tcu_tls_cfg *cfg, struct tcu_tls *out);
void tcu_tls_close(struct tcu_tls *t);

#endif
