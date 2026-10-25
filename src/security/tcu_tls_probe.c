// SPDX-License-Identifier: MIT
/*
 * tcu-tls-probe - check backend reachability with the production TLS
 * policy (used by field diagnostics and the HIL suite).
 *
 *   tcu-tls-probe -h host -p 443 -C ca.pem [-c dev.crt -k dev.key] [-o]
 */
#include <getopt.h>
#include <stdio.h>

#include "tls_client.h"

int main(int argc, char **argv)
{
	struct tcu_tls_cfg cfg = { .port = "443" };
	struct tcu_tls t;
	int opt;

	while ((opt = getopt(argc, argv, "h:p:C:c:k:o")) != -1) {
		switch (opt) {
		case 'h': cfg.host = optarg; break;
		case 'p': cfg.port = optarg; break;
		case 'C': cfg.ca_file = optarg; break;
		case 'c': cfg.cert_file = optarg; break;
		case 'k': cfg.key_file = optarg; break;
		case 'o': cfg.require_ocsp = true; break;
		default: return 2;
		}
	}
	if (!cfg.host || !cfg.ca_file || (cfg.cert_file && !cfg.key_file)) {
		fprintf(stderr, "usage: %s -h host [-p port] -C ca [-c cert -k key] [-o]\n", argv[0]);
		return 2;
	}
	if (tcu_tls_connect(&cfg, &t) != 0)
		return 1;
	printf("ok %s %s\n", SSL_get_version(t.ssl), SSL_get_cipher(t.ssl));
	tcu_tls_close(&t);
	return 0;
}
