// SPDX-License-Identifier: MIT
#include "at.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void at_reader_init(struct at_reader *r, void (*on_line)(void *, const char *), void *ctx)
{
	r->len = 0;
	r->on_line = on_line;
	r->ctx = ctx;
}

void at_reader_feed(struct at_reader *r, const char *data, size_t len)
{
	for (size_t i = 0; i < len; i++) {
		char c = data[i];

		if (c == '\r' || c == '\n') {
			if (r->len) {
				r->buf[r->len] = '\0';
				r->on_line(r->ctx, r->buf);
				r->len = 0;
			}
			continue;
		}
		/* Drop over-long garbage instead of splitting it into fake lines. */
		if (r->len < sizeof(r->buf) - 1)
			r->buf[r->len++] = c;
	}
}

static bool eq(const char *a, const char *b)
{
	return strcmp(a, b) == 0;
}

bool at_has_prefix(const char *line, const char *prefix)
{
	size_t n = strlen(prefix);

	return strncmp(line, prefix, n) == 0 && line[n] == ':';
}

at_line_type_t at_classify(const char *line)
{
	if (!line || !*line)
		return AT_LINE_EMPTY;
	if (eq(line, "OK"))
		return AT_LINE_OK;
	if (eq(line, "ERROR") || at_has_prefix(line, "+CME ERROR") ||
	    at_has_prefix(line, "+CMS ERROR"))
		return AT_LINE_ERROR;
	if (eq(line, "NO CARRIER"))
		return AT_LINE_NO_CARRIER;
	if (eq(line, "BUSY"))
		return AT_LINE_BUSY;
	if (eq(line, "NO ANSWER"))
		return AT_LINE_NO_ANSWER;
	if (eq(line, "RING"))
		return AT_LINE_RING;
	if (strncmp(line, "CONNECT", 7) == 0)
		return AT_LINE_CONNECT;
	return AT_LINE_INFO;
}

/* Return pointer to the first parameter after "+XXX: ". */
static const char *params(const char *line)
{
	const char *p = strchr(line, ':');

	if (!p)
		return NULL;
	p++;
	while (*p == ' ')
		p++;
	return p;
}

static int next_int(const char **p, int *out)
{
	char *end;
	long v;

	while (**p == ' ')
		(*p)++;
	if (!isdigit((unsigned char)**p) && **p != '-')
		return -1;
	v = strtol(*p, &end, 10);
	if (end == *p)
		return -1;
	*out = (int)v;
	*p = end;
	if (**p == ',')
		(*p)++;
	return 0;
}

int at_parse_reg(const char *line, bool solicited, int *stat)
{
	const char *p;
	int n;

	if (!at_has_prefix(line, "+CREG") && !at_has_prefix(line, "+CGREG") &&
	    !at_has_prefix(line, "+CEREG"))
		return -1;
	p = params(line);
	if (solicited && next_int(&p, &n) < 0)
		return -1;
	if (next_int(&p, stat) < 0 || *stat < 0 || *stat > 10)
		return -1;
	return 0;
}

int at_parse_csq(const char *line, int *rssi, int *ber)
{
	const char *p;

	if (!at_has_prefix(line, "+CSQ"))
		return -1;
	p = params(line);
	if (next_int(&p, rssi) < 0 || next_int(&p, ber) < 0)
		return -1;
	return 0;
}

int at_parse_cme_error(const char *line, int *code)
{
	const char *p;

	if (!at_has_prefix(line, "+CME ERROR"))
		return -1;
	p = params(line);
	return next_int(&p, code);
}

int at_parse_cpin_ready(const char *line)
{
	if (!at_has_prefix(line, "+CPIN"))
		return -1;
	return strcmp(params(line), "READY") == 0;
}

int at_csq_to_dbm(int rssi)
{
	if (rssi < 0 || rssi > 31)
		return 0;
	return -113 + 2 * rssi;
}
