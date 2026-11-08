// SPDX-License-Identifier: MIT
#include "check.h"
#include "rat.h"

static struct at_cesq cesq(int rscp, int rsrp, int ss_rsrp)
{
	struct at_cesq q = {
		.rxlev = 99, .ber = 99, .rscp = rscp, .ecno = 255, .rsrq = 255,
		.rsrp = rsrp, .ss_rsrq = 255, .ss_rsrp = ss_rsrp, .ss_sinr = 255,
	};
	return q;
}

static void test_basics(void)
{
	struct at_cesq q = cesq(30, 41, 57);
	char buf[96];
	rat_mask_t m;
	rat_vendor_t v;

	CHECK_EQ(rat_from_act(0), RAT_2G);
	CHECK_EQ(rat_from_act(3), RAT_2G);
	CHECK_EQ(rat_from_act(2), RAT_3G);
	CHECK_EQ(rat_from_act(6), RAT_3G);
	CHECK_EQ(rat_from_act(7), RAT_4G);
	CHECK_EQ(rat_from_act(13), RAT_5G_NSA);
	CHECK_EQ(rat_from_act(11), RAT_5G_SA);
	CHECK_EQ(rat_from_act(-1), RAT_NONE);
	CHECK_EQ(rat_bit(RAT_5G_NSA), RAT_BIT_5G);
	CHECK_STR(rat_name(RAT_5G_SA), "5G-SA");

	CHECK_EQ(rat_mask_parse("3g,4g,5g", &m), 0);
	CHECK_EQ(m, RAT_BIT_3G | RAT_BIT_4G | RAT_BIT_5G);
	CHECK_EQ(rat_mask_parse("5G, 2G", &m), 0);
	CHECK_EQ(m, RAT_BIT_2G | RAT_BIT_5G);
	CHECK_EQ(rat_mask_parse("all", &m), 0);
	CHECK_EQ(m, RAT_MASK_ALL);
	CHECK_EQ(rat_mask_parse("6g", &m), -1);
	CHECK_EQ(rat_mask_parse("4gx", &m), -1);
	CHECK_EQ(rat_mask_parse("", &m), -1);
	CHECK_STR(rat_mask_str(RAT_BIT_3G | RAT_BIT_5G | RAT_BIT_4G, buf, sizeof(buf)), "5G/4G/3G");
	CHECK_STR(rat_mask_str(0, buf, sizeof(buf)), "-");

	CHECK_EQ(rat_signal_dbm(&q, RAT_3G), -91);
	CHECK_EQ(rat_signal_dbm(&q, RAT_4G), -100);
	CHECK_EQ(rat_signal_dbm(&q, RAT_5G_SA), -100);
	CHECK_EQ(rat_signal_dbm(&q, RAT_2G), 0); /* 99 = not measured */

	CHECK_EQ(rat_vendor_parse("Quectel", &v), 0);
	CHECK_EQ(v, RAT_VENDOR_QUECTEL);
	CHECK_EQ(rat_vendor_parse("acme", &v), -1);
	CHECK_EQ(rat_pref_cmd(RAT_VENDOR_3GPP, RAT_BIT_3G | RAT_BIT_4G | RAT_BIT_5G, buf, sizeof(buf)), 0);
	CHECK_STR(buf, "AT+WS46=38");
	CHECK_EQ(rat_pref_cmd(RAT_VENDOR_3GPP, RAT_MASK_ALL, buf, sizeof(buf)), 0);
	CHECK_STR(buf, "AT+WS46=35");
	CHECK_EQ(rat_pref_cmd(RAT_VENDOR_3GPP, RAT_BIT_4G, buf, sizeof(buf)), 0);
	CHECK_STR(buf, "AT+WS46=28");
	CHECK_EQ(rat_pref_cmd(RAT_VENDOR_QUECTEL, RAT_BIT_3G | RAT_BIT_4G | RAT_BIT_5G, buf, sizeof(buf)), 0);
	CHECK_STR(buf, "AT+QNWPREFCFG=\"mode_pref\",NR5G:LTE:WCDMA");
	CHECK_EQ(rat_pref_cmd(RAT_VENDOR_QUECTEL, RAT_BIT_4G, buf, sizeof(buf)), 0);
	CHECK_STR(buf, "AT+QNWPREFCFG=\"mode_pref\",LTE");
	CHECK_EQ(rat_pref_cmd(RAT_VENDOR_3GPP, 0, buf, sizeof(buf)), -1);
}

static void test_registration(void)
{
	struct rat_policy_cfg cfg = { .allowed = RAT_BIT_3G | RAT_BIT_4G | RAT_BIT_5G };
	struct rat_policy p;

	rat_policy_init(&p, &cfg);
	CHECK_EQ(rat_policy_reg_stat(&p), AT_REG_NOT_SEARCHING);
	CHECK_EQ(p.serving, RAT_NONE);

	/* LTE first. */
	rat_policy_on_reg(&p, RAT_DOM_EPS, AT_REG_HOME, 7, 0);
	CHECK_EQ(rat_policy_reg_stat(&p), AT_REG_HOME);
	CHECK_EQ(p.serving, RAT_4G);

	/* Secondary NR leg added: +COPS reports EN-DC. */
	rat_policy_on_cops(&p, 13, 100);
	CHECK_EQ(p.serving, RAT_5G_NSA);

	/* Fall back to 3G: EPS lost but PS registered, still connected. */
	rat_policy_on_reg(&p, RAT_DOM_PS, AT_REG_HOME, 6, 200);
	rat_policy_on_reg(&p, RAT_DOM_EPS, AT_REG_SEARCHING, -1, 210);
	rat_policy_on_cops(&p, 6, 220);
	CHECK_EQ(rat_policy_reg_stat(&p), AT_REG_HOME);
	CHECK_EQ(p.serving, RAT_3G);

	/* 5G SA registration takes over as the best RAT. */
	rat_policy_on_reg(&p, RAT_DOM_5GS, AT_REG_ROAMING, 11, 300);
	CHECK_EQ(p.serving, RAT_5G_SA);
	rat_policy_on_reg(&p, RAT_DOM_PS, AT_REG_SEARCHING, -1, 310);
	CHECK_EQ(rat_policy_reg_stat(&p), AT_REG_ROAMING);

	/* Everything gone. */
	rat_policy_on_reg(&p, RAT_DOM_5GS, AT_REG_NOT_SEARCHING, -1, 400);
	CHECK_EQ(rat_policy_reg_stat(&p), AT_REG_SEARCHING);
	CHECK_EQ(p.serving, RAT_NONE);

	/* Denied on LTE while 3G is still searching: not final yet. */
	rat_policy_on_reg(&p, RAT_DOM_EPS, AT_REG_DENIED, -1, 500);
	CHECK_EQ(rat_policy_reg_stat(&p), AT_REG_SEARCHING);
	rat_policy_on_reg(&p, RAT_DOM_PS, AT_REG_DENIED, -1, 510);
	CHECK_EQ(rat_policy_reg_stat(&p), AT_REG_DENIED);

	/* 2G not allowed: a GSM registration does not count. */
	rat_policy_init(&p, &cfg);
	rat_policy_on_reg(&p, RAT_DOM_PS, AT_REG_HOME, 3, 0);
	CHECK_EQ(rat_policy_reg_stat(&p), AT_REG_NOT_SEARCHING);
	CHECK_EQ(p.serving, RAT_NONE);
	cfg.allowed |= RAT_BIT_2G;
	rat_policy_init(&p, &cfg);
	rat_policy_on_reg(&p, RAT_DOM_PS, AT_REG_HOME, 3, 0);
	CHECK_EQ(rat_policy_reg_stat(&p), AT_REG_HOME);
	CHECK_EQ(p.serving, RAT_2G);
}

static void test_steering(void)
{
	struct rat_policy_cfg cfg = {
		.allowed = RAT_BIT_3G | RAT_BIT_4G | RAT_BIT_5G,
		.weak_dwell_ms = 30000, .hold_ms = 600000, .max_hold_ms = 1800000,
	};
	struct rat_policy p;
	struct at_cesq weak5g = cesq(255, 51, 37);  /* NR -120 dBm, LTE -90 */
	struct at_cesq good5g = cesq(255, 51, 67);  /* NR -90 dBm */
	struct at_cesq weak4g = cesq(40, 20, 255);  /* LTE -121 dBm */
	rat_mask_t m;

	rat_policy_init(&p, &cfg);
	CHECK(rat_policy_take_pref(&p, &m));
	CHECK_EQ(m, RAT_BIT_3G | RAT_BIT_4G | RAT_BIT_5G);
	CHECK(!rat_policy_take_pref(&p, &m));

	rat_policy_on_reg(&p, RAT_DOM_5GS, AT_REG_HOME, 11, 0);
	CHECK_EQ(p.serving, RAT_5G_SA);

	/* Weak NR, but not for long enough: no change. */
	rat_policy_on_cesq(&p, &weak5g, 1000);
	rat_policy_tick(&p, 20000);
	CHECK(!rat_policy_take_pref(&p, &m));

	/* Recovers (with hysteresis), timer restarts. */
	rat_policy_on_cesq(&p, &good5g, 25000);
	rat_policy_on_cesq(&p, &weak5g, 40000);
	rat_policy_tick(&p, 60000);
	CHECK(!rat_policy_take_pref(&p, &m));

	/* Stays weak past the dwell: 5G excluded. */
	rat_policy_tick(&p, 71000);
	CHECK(rat_policy_take_pref(&p, &m));
	CHECK_EQ(m, RAT_BIT_3G | RAT_BIT_4G);

	/* Modem moves to LTE; after the hold time 5G is retried. */
	rat_policy_on_reg(&p, RAT_DOM_5GS, AT_REG_NOT_SEARCHING, -1, 72000);
	rat_policy_on_reg(&p, RAT_DOM_EPS, AT_REG_HOME, 7, 72000);
	CHECK_EQ(p.serving, RAT_4G);
	rat_policy_tick(&p, 71000 + 600000 - 1);
	CHECK(!rat_policy_take_pref(&p, &m));
	rat_policy_tick(&p, 71000 + 600000);
	CHECK(rat_policy_take_pref(&p, &m));
	CHECK_EQ(m, RAT_BIT_3G | RAT_BIT_4G | RAT_BIT_5G);

	/* Second exclusion of 5G holds twice as long (data failure). */
	rat_policy_on_reg(&p, RAT_DOM_5GS, AT_REG_HOME, 11, 700000);
	rat_policy_on_data_fail(&p, 700000);
	CHECK(rat_policy_take_pref(&p, &m));
	CHECK_EQ(m, RAT_BIT_3G | RAT_BIT_4G);
	CHECK_EQ(p.demote_until[3], 700000 + 1200000);

	/* Weak LTE with 3G available: LTE excluded too, 3G remains. */
	rat_policy_on_reg(&p, RAT_DOM_5GS, AT_REG_NOT_SEARCHING, -1, 701000);
	rat_policy_on_cesq(&p, &weak4g, 701000);
	rat_policy_tick(&p, 731000);
	CHECK(rat_policy_take_pref(&p, &m));
	CHECK_EQ(m, RAT_BIT_3G);

	/* 3G is the last usable RAT: never excluded, however bad. */
	rat_policy_on_reg(&p, RAT_DOM_EPS, AT_REG_NOT_SEARCHING, -1, 732000);
	rat_policy_on_reg(&p, RAT_DOM_PS, AT_REG_HOME, 6, 732000);
	CHECK_EQ(p.serving, RAT_3G);
	{
		struct at_cesq bad3g = cesq(5, 255, 255);

		rat_policy_on_cesq(&p, &bad3g, 733000);
		rat_policy_tick(&p, 800000);
		rat_policy_on_data_fail(&p, 800000);
	}
	CHECK(!rat_policy_take_pref(&p, &m));
	CHECK_EQ(rat_policy_pref(&p), RAT_BIT_3G);

	/* Holds are capped. */
	CHECK(p.demote_until[3] - 700000 <= cfg.max_hold_ms);
}

int main(void)
{
	test_basics();
	test_registration();
	test_steering();
	CHECK_DONE();
}
