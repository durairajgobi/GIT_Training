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
	/* Multi-RAT registration with <AcT>. */
	{
		struct at_cesq q;
		int act, mode;

		CHECK_EQ(at_parse_reg_act("+CEREG: 1,\"1A2B\",\"01A2B3C4\",13", false, &stat, &act), 0);
		CHECK_EQ(stat, AT_REG_HOME);
		CHECK_EQ(act, 13);
		CHECK_EQ(at_parse_reg_act("+CGREG: 5,\"00C3\",\"0000A13F\",6,\"01\"", false, &stat, &act), 0);
		CHECK_EQ(stat, AT_REG_ROAMING);
		CHECK_EQ(act, 6);
		CHECK_EQ(at_parse_reg_act("+C5GREG: 2,1,\"00A1B2\",\"000000123456\",11,0", true, &stat, &act), 0);
		CHECK_EQ(stat, AT_REG_HOME);
		CHECK_EQ(act, 11);
		/* Bare stat, and empty location fields. */
		CHECK_EQ(at_parse_reg_act("+CEREG: 2", false, &stat, &act), 0);
		CHECK_EQ(stat, AT_REG_SEARCHING);
		CHECK_EQ(act, -1);
		CHECK_EQ(at_parse_reg_act("+CEREG: 1,,,7", false, &stat, &act), 0);
		CHECK_EQ(act, 7);
		CHECK_EQ(at_parse_reg("+C5GREG: 3", false, &stat), 0);
		CHECK_EQ(stat, AT_REG_DENIED);
		CHECK_EQ(at_parse_reg_act("+CSQ: 1,2", false, &stat, &act), -1);

		CHECK_EQ(at_parse_cops("+COPS: 0,0,\"Op, Inc\",13", &mode, &act), 0);
		CHECK_EQ(mode, 0);
		CHECK_EQ(act, 13);
		CHECK_EQ(at_parse_cops("+COPS: 0", &mode, &act), 0);
		CHECK_EQ(act, -1);
		CHECK_EQ(at_parse_cops("+COPS: 0,2,\"26201\",2", &mode, &act), 0);
		CHECK_EQ(act, 2);

		CHECK_EQ(at_parse_cesq("+CESQ: 99,99,255,255,20,40", &q), 0);
		CHECK_EQ(q.rsrp, 40);
		CHECK_EQ(q.ss_rsrp, 255);
		CHECK_EQ(at_parse_cesq("+CESQ: 99,99,255,255,20,40,60,70,80", &q), 0);
		CHECK_EQ(q.ss_rsrp, 70);
		CHECK_EQ(q.ss_sinr, 80);
		CHECK_EQ(at_parse_cesq("+CESQ: 99,99", &q), -1);
	}
	CHECK_DONE();
}
