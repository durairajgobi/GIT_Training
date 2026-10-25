// SPDX-License-Identifier: MIT
#include "tls_client.h"

#include <netdb.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <openssl/err.h>
#include <openssl/ocsp.h>
#include <openssl/x509v3.h>

#include "tcu_log.h"

#define OCSP_MAX_SKEW_S 300

static void log_ssl_errors(const char *what)
{
	unsigned long e;
	char buf[256];

	while ((e = ERR_get_error()) != 0) {
		ERR_error_string_n(e, buf, sizeof(buf));
		LOG_E("%s: %s", what, buf);
	}
}

static X509 *find_issuer(SSL *ssl, X509 *leaf)
{
	STACK_OF(X509) *chain = SSL_get_peer_cert_chain(ssl);
	X509_STORE *store = SSL_CTX_get_cert_store(SSL_get_SSL_CTX(ssl));
	X509_STORE_CTX *sctx;
	X509 *issuer = NULL;

	for (int i = 0; chain && i < sk_X509_num(chain); i++) {
		X509 *c = sk_X509_value(chain, i);

		if (X509_check_issued(c, leaf) == X509_V_OK) {
			X509_up_ref(c);
			return c;
		}
	}
	/* Intermediate not sent: look it up in our trust store. */
	sctx = X509_STORE_CTX_new();
	if (sctx && X509_STORE_CTX_init(sctx, store, leaf, NULL) == 1)
		X509_STORE_CTX_get1_issuer(&issuer, sctx, leaf);
	X509_STORE_CTX_free(sctx);
	return issuer;
}

/*
 * Client-side status callback: return 1 to accept, 0 to fail the handshake.
 * The staple is validated against the same trust store as the server chain.
 */
static int ocsp_status_cb(SSL *ssl, void *arg)
{
	const bool require = *(const bool *)arg;
	const unsigned char *p;
	long len = SSL_get_tlsext_status_ocsp_resp(ssl, &p);
	OCSP_RESPONSE *resp = NULL;
	OCSP_BASICRESP *basic = NULL;
	OCSP_CERTID *id = NULL;
	X509 *leaf = SSL_get1_peer_certificate(ssl), *issuer = NULL;
	ASN1_GENERALIZEDTIME *thisupd, *nextupd;
	int status, reason, ok = 0;

	if (len <= 0 || !p) {
		LOG_W("server sent no OCSP staple");
		ok = !require;
		goto out;
	}
	resp = d2i_OCSP_RESPONSE(NULL, &p, len);
	if (!resp || OCSP_response_status(resp) != OCSP_RESPONSE_STATUS_SUCCESSFUL)
		goto out;
	basic = OCSP_response_get1_basic(resp);
	if (!basic || !leaf || !(issuer = find_issuer(ssl, leaf)))
		goto out;
	if (OCSP_basic_verify(basic, SSL_get_peer_cert_chain(ssl),
			      SSL_CTX_get_cert_store(SSL_get_SSL_CTX(ssl)), 0) <= 0)
		goto out;

	id = OCSP_cert_to_id(NULL, leaf, issuer);
	if (!id || !OCSP_resp_find_status(basic, id, &status, &reason, NULL,
					   &thisupd, &nextupd))
		goto out;
	if (!OCSP_check_validity(thisupd, nextupd, OCSP_MAX_SKEW_S, -1)) {
		LOG_W("OCSP staple outside validity window");
		goto out;
	}
	if (status != V_OCSP_CERTSTATUS_GOOD) {
		LOG_E("server certificate %s (reason %d)",
		      status == V_OCSP_CERTSTATUS_REVOKED ? "REVOKED" : "status unknown", reason);
		goto out;
	}
	ok = 1;
out:
	if (!ok)
		log_ssl_errors("OCSP");
	OCSP_CERTID_free(id);
	OCSP_BASICRESP_free(basic);
	OCSP_RESPONSE_free(resp);
	X509_free(issuer);
	X509_free(leaf);
	return ok;
}

static int tcp_connect(const char *host, const char *port, int timeout_s)
{
	struct addrinfo hints = { .ai_socktype = SOCK_STREAM }, *res, *ai;
	struct timeval tv = { .tv_sec = timeout_s };
	int fd = -1;

	if (getaddrinfo(host, port, &hints, &res) != 0)
		return -1;
	for (ai = res; ai; ai = ai->ai_next) {
		fd = socket(ai->ai_family, ai->ai_socktype | SOCK_CLOEXEC, ai->ai_protocol);
		if (fd < 0)
			continue;
		setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
		setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
		if (connect(fd, ai->ai_addr, ai->ai_addrlen) == 0)
			break;
		close(fd);
		fd = -1;
	}
	freeaddrinfo(res);
	return fd;
}

static bool require_ocsp_true = true, require_ocsp_false = false;

int tcu_tls_connect(const struct tcu_tls_cfg *cfg, struct tcu_tls *t)
{
	memset(t, 0, sizeof(*t));
	t->fd = -1;

	t->ctx = SSL_CTX_new(TLS_client_method());
	if (!t->ctx)
		goto err;
	SSL_CTX_set_min_proto_version(t->ctx, TLS1_2_VERSION);
	if (!SSL_CTX_set_cipher_list(t->ctx,
				     "ECDHE-ECDSA-AES256-GCM-SHA384:ECDHE-RSA-AES256-GCM-SHA384:"
				     "ECDHE-ECDSA-CHACHA20-POLY1305:ECDHE-ECDSA-AES128-GCM-SHA256"))
		goto err;
	SSL_CTX_set_options(t->ctx, SSL_OP_NO_RENEGOTIATION | SSL_OP_NO_COMPRESSION);
	SSL_CTX_set_verify(t->ctx, SSL_VERIFY_PEER, NULL);
	if (SSL_CTX_load_verify_locations(t->ctx, cfg->ca_file, NULL) != 1)
		goto err;
	if (cfg->cert_file &&
	    (SSL_CTX_use_certificate_chain_file(t->ctx, cfg->cert_file) != 1 ||
	     SSL_CTX_use_PrivateKey_file(t->ctx, cfg->key_file, SSL_FILETYPE_PEM) != 1 ||
	     SSL_CTX_check_private_key(t->ctx) != 1))
		goto err;
	SSL_CTX_set_tlsext_status_cb(t->ctx, ocsp_status_cb);
	SSL_CTX_set_tlsext_status_arg(t->ctx, cfg->require_ocsp ? &require_ocsp_true
								 : &require_ocsp_false);

	t->fd = tcp_connect(cfg->host, cfg->port, cfg->timeout_s ? cfg->timeout_s : 15);
	if (t->fd < 0) {
		LOG_E("connect %s:%s failed", cfg->host, cfg->port);
		goto fail;
	}
	t->ssl = SSL_new(t->ctx);
	if (!t->ssl)
		goto err;
	SSL_set_fd(t->ssl, t->fd);
	SSL_set_tlsext_host_name(t->ssl, cfg->host);
	SSL_set1_host(t->ssl, cfg->host);
	SSL_set_tlsext_status_type(t->ssl, TLSEXT_STATUSTYPE_ocsp);

	if (SSL_connect(t->ssl) != 1) {
		long v = SSL_get_verify_result(t->ssl);

		if (v != X509_V_OK)
			LOG_E("certificate verify: %s", X509_verify_cert_error_string(v));
		goto err;
	}
	LOG_I("TLS up: %s %s", SSL_get_version(t->ssl), SSL_get_cipher(t->ssl));
	return 0;

err:
	log_ssl_errors("TLS");
fail:
	tcu_tls_close(t);
	return -1;
}

void tcu_tls_close(struct tcu_tls *t)
{
	if (t->ssl) {
		SSL_shutdown(t->ssl);
		SSL_free(t->ssl);
	}
	if (t->fd >= 0)
		close(t->fd);
	SSL_CTX_free(t->ctx);
	memset(t, 0, sizeof(*t));
	t->fd = -1;
}
