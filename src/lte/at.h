/* SPDX-License-Identifier: MIT */
#ifndef TCU_AT_H
#define TCU_AT_H

#include <stdbool.h>
#include <stddef.h>

/* 3GPP TS 27.007 response / URC handling shared by ltecm and ecalld. */

typedef enum {
	AT_LINE_EMPTY,
	AT_LINE_OK,
	AT_LINE_ERROR,      /* ERROR, +CME ERROR, +CMS ERROR */
	AT_LINE_NO_CARRIER,
	AT_LINE_BUSY,
	AT_LINE_NO_ANSWER,
	AT_LINE_RING,
	AT_LINE_CONNECT,
	AT_LINE_INFO,       /* "+XXX: ..." or other text */
} at_line_type_t;

/* +CREG / +CEREG <stat> values */
enum at_reg_stat {
	AT_REG_NOT_SEARCHING = 0,
	AT_REG_HOME = 1,
	AT_REG_SEARCHING = 2,
	AT_REG_DENIED = 3,
	AT_REG_UNKNOWN = 4,
	AT_REG_ROAMING = 5,
};

/* Byte stream -> CR/LF separated lines. */
struct at_reader {
	char buf[1024];
	size_t len;
	void (*on_line)(void *ctx, const char *line);
	void *ctx;
};

void at_reader_init(struct at_reader *r, void (*on_line)(void *, const char *), void *ctx);
void at_reader_feed(struct at_reader *r, const char *data, size_t len);

at_line_type_t at_classify(const char *line);

/* True if the line starts with "<prefix>:" e.g. at_has_prefix(l, "+CEREG"). */
bool at_has_prefix(const char *line, const char *prefix);

/*
 * Parse +CREG/+CGREG/+CEREG. A solicited response to "AT+CEREG?" is
 * "<n>,<stat>[,...]" while an unsolicited one is "<stat>[,...]".
 */
int at_parse_reg(const char *line, bool solicited, int *stat);
int at_parse_csq(const char *line, int *rssi, int *ber);
int at_parse_cme_error(const char *line, int *code);
/* Returns 1 if "+CPIN: READY", 0 for any other PIN state, -1 if not +CPIN. */
int at_parse_cpin_ready(const char *line);

/* Convert CSQ rssi (0..31, 99) to dBm; returns 0 for unknown. */
int at_csq_to_dbm(int rssi);

#endif
