// SPDX-License-Identifier: MIT
#include "at.h"
#include "check.h"

static char lines[8][64];
static int nlines;

static void on_line(void *ctx, const char *l)
{
	if (nlines < 8)
		snprintf(lines[nlines++], sizeof(lines[0]), "%s", l);
}

int main(void)
{
	struct at_reader r;
	int stat, rssi, ber, code;

	/* Lines split across reads, mixed CR/LF, blank lines dropped. */
	at_reader_init(&r, on_line, NULL);
	static const char *chunks[] = { "\r\nOK\r\n+CEREG: 1,\"1A2B\",\"01A2", "B3C4\",7\r\n\r\nRING\n" };

	for (size_t i = 0; i < 2; i++)
		at_reader_feed(&r, chunks[i], strlen(chunks[i]));
	CHECK_EQ(nlines, 3);
	CHECK_STR(lines[0], "OK");
	CHECK_STR(lines[1], "+CEREG: 1,\"1A2B\",\"01A2B3C4\",7");
	CHECK_STR(lines[2], "RING");

	CHECK_EQ(at_classify("OK"), AT_LINE_OK);
	CHECK_EQ(at_classify("+CME ERROR: 10"), AT_LINE_ERROR);
	CHECK_EQ(at_classify("NO CARRIER"), AT_LINE_NO_CARRIER);
	CHECK_EQ(at_classify("+CSQ: 20,99"), AT_LINE_INFO);
	CHECK_EQ(at_classify(""), AT_LINE_EMPTY);

	/* URC: stat first. Solicited: <n>,<stat>. */
	CHECK_EQ(at_parse_reg(lines[1], false, &stat), 0);
	CHECK_EQ(stat, AT_REG_HOME);
	CHECK_EQ(at_parse_reg("+CEREG: 2,5,\"1A2B\",\"01A2B3C4\",7", true, &stat), 0);
	CHECK_EQ(stat, AT_REG_ROAMING);
	CHECK_EQ(at_parse_reg("+CREG: 3", false, &stat), 0);
	CHECK_EQ(stat, AT_REG_DENIED);
	CHECK_EQ(at_parse_reg("+COPS: 0", false, &stat), -1);
	CHECK_EQ(at_parse_reg("+CEREG: x", false, &stat), -1);

	CHECK_EQ(at_parse_csq("+CSQ: 20,99", &rssi, &ber), 0);
	CHECK_EQ(rssi, 20);
	CHECK_EQ(at_csq_to_dbm(rssi), -73);
	CHECK_EQ(at_csq_to_dbm(99), 0);

	CHECK_EQ(at_parse_cme_error("+CME ERROR: 10", &code), 0);
	CHECK_EQ(code, 10);
	CHECK_EQ(at_parse_cpin_ready("+CPIN: READY"), 1);
	CHECK_EQ(at_parse_cpin_ready("+CPIN: SIM PIN"), 0);
	CHECK_EQ(at_parse_cpin_ready("OK"), -1);
	CHECK(!at_has_prefix("+CEREGX: 1", "+CEREG"));
	CHECK_DONE();
}
