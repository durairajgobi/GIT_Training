// SPDX-License-Identifier: MIT
#include "at.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))

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

static bool is_reg_line(const char *line)
{
	return at_has_prefix(line, "+CREG") || at_has_prefix(line, "+CGREG") ||
	       at_has_prefix(line, "+CEREG") || at_has_prefix(line, "+C5GREG");
}

/*
 * Advance past one comma-separated field that may be empty, a bare
 * number or a quoted string. Returns false at end of line.
 */
static bool skip_field(const char **p)
{
	const char *s = *p;

	while (*s == ' ')
		s++;
	if (*s == '"') {
		s = strchr(s + 1, '"');
		if (!s)
			return false;
		s++;
	}
	while (*s && *s != ',')
		s++;
	if (*s != ',') {
		*p = s;
		return false;
	}
	*p = s + 1;
	return true;
}

int at_parse_reg(const char *line, bool solicited, int *stat)
{
	const char *p;
	int n;

	if (!is_reg_line(line))
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

int at_parse_reg_act(const char *line, bool solicited, int *stat, int *act)
{
	const char *p;
	int n;

	*act = -1;
	if (!is_reg_line(line))
		return -1;
	p = params(line);
	if (solicited && next_int(&p, &n) < 0)
		return -1;
	if (next_int(&p, stat) < 0 || *stat < 0 || *stat > 10)
		return -1;
	/* <lac|tac>, <ci> (both may be empty or quoted), then <AcT>. */
	if (!*p || !skip_field(&p) || !skip_field(&p))
		return 0;
	if (next_int(&p, &n) == 0)
		*act = n;
	return 0;
}

int at_parse_cops(const char *line, int *mode, int *act)
{
	const char *p;
	int v;

	*act = -1;
	if (!at_has_prefix(line, "+COPS"))
		return -1;
	p = params(line);
	if (next_int(&p, mode) < 0)
		return -1;
	/* <format>, <oper> (quoted), then <AcT>. */
	if (!*p || !skip_field(&p) || !skip_field(&p))
		return 0;
	if (next_int(&p, &v) == 0)
		*act = v;
	return 0;
}

int at_parse_cesq(const char *line, struct at_cesq *q)
{
	int *f[] = { &q->rxlev, &q->ber, &q->rscp, &q->ecno, &q->rsrq,
		     &q->rsrp, &q->ss_rsrq, &q->ss_rsrp, &q->ss_sinr };
	const char *p;
	size_t i;

	if (!at_has_prefix(line, "+CESQ"))
		return -1;
	q->ss_rsrq = q->ss_rsrp = q->ss_sinr = 255;
	p = params(line);
	for (i = 0; i < ARRAY_LEN(f); i++) {
		if (!*p)
			break;
		if (next_int(&p, f[i]) < 0)
			return -1;
	}
	return i >= 6 ? 0 : -1;
}
