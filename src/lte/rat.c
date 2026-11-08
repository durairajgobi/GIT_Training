// SPDX-License-Identifier: MIT
#include "rat.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

/* Signal must rise this far above the threshold to count as recovered. */
#define RAT_HYST_DB 3

static const char *const names[RAT_COUNT] = {
	[RAT_NONE] = "none", [RAT_2G] = "2G", [RAT_3G] = "3G", [RAT_4G] = "4G",
	[RAT_5G_NSA] = "5G-NSA", [RAT_5G_SA] = "5G-SA",
};

/* Defaults chosen around the point where throughput collapses and the
 * modem starts to reselect; tune per antenna installation. */
static const int default_min_dbm[RAT_COUNT] = {
	[RAT_2G] = -100,     /* RXLEV  */
	[RAT_3G] = -106,     /* RSCP   */
	[RAT_4G] = -115,     /* RSRP   */
	[RAT_5G_NSA] = -110, /* SS-RSRP of the NR leg */
	[RAT_5G_SA] = -112,  /* SS-RSRP */
};

const char *rat_name(rat_t r)
{
	return r < RAT_COUNT ? names[r] : "?";
}

rat_mask_t rat_bit(rat_t r)
{
	switch (r) {
	case RAT_2G: return RAT_BIT_2G;
	case RAT_3G: return RAT_BIT_3G;
	case RAT_4G: return RAT_BIT_4G;
	case RAT_5G_NSA:
	case RAT_5G_SA: return RAT_BIT_5G;
	default: return 0;
	}
}

static int bit_index(rat_mask_t bit)
{
	for (int i = 0; i < 4; i++)
		if (bit == (1u << i))
			return i;
	return -1;
}

rat_t rat_from_act(int act)
{
	switch (act) {
	case 0:  /* GSM */
	case 1:  /* GSM Compact */
	case 3:  /* GSM w/EGPRS */
	case 8:  /* EC-GSM-IoT */
		return RAT_2G;
	case 2:  /* UTRAN */
	case 4:  /* UTRAN w/HSDPA */
	case 5:  /* UTRAN w/HSUPA */
	case 6:  /* UTRAN w/HSDPA and HSUPA */
		return RAT_3G;
	case 7:  /* E-UTRAN */
	case 9:  /* E-UTRAN NB-S1 */
	case 10: /* E-UTRA connected to 5GCN */
		return RAT_4G;
	case 11: /* NR connected to 5GCN */
	case 12: /* NG-RAN */
		return RAT_5G_SA;
	case 13: /* E-UTRA-NR dual connectivity */
		return RAT_5G_NSA;
	default:
		return RAT_NONE;
	}
}

int rat_mask_parse(const char *s, rat_mask_t *out)
{
	rat_mask_t m = 0;

	if (!s)
		return -1;
	if (strcasecmp(s, "all") == 0) {
		*out = RAT_MASK_ALL;
		return 0;
	}
	while (*s) {
		while (*s == ',' || *s == ' ')
			s++;
		if (!*s)
			break;
		if (s[0] < '2' || s[0] > '5' || tolower((unsigned char)s[1]) != 'g' ||
		    (s[2] && s[2] != ',' && s[2] != ' '))
			return -1;
		m |= 1u << (s[0] - '2');
		s += 2;
	}
	if (!m)
		return -1;
	*out = m;
	return 0;
}

const char *rat_mask_str(rat_mask_t m, char *buf, size_t len)
{
	static const char *const tag[4] = { "2G", "3G", "4G", "5G" };
	size_t off = 0;

	if (!len)
		return buf;
	buf[0] = '\0';
	for (int i = 3; i >= 0; i--) {
		if (!(m & (1u << i)))
			continue;
		int n = snprintf(buf + off, len - off, "%s%s", off ? "/" : "", tag[i]);

		if (n < 0 || (size_t)n >= len - off)
			break;
		off += (size_t)n;
	}
	if (!off)
		snprintf(buf, len, "-");
	return buf;
}

int rat_signal_dbm(const struct at_cesq *q, rat_t r)
{
	switch (r) {
	case RAT_2G:
		return q->rxlev >= 0 && q->rxlev <= 63 ? -111 + q->rxlev : 0;
	case RAT_3G:
		return q->rscp >= 0 && q->rscp <= 96 ? -121 + q->rscp : 0;
	case RAT_4G:
		return q->rsrp >= 0 && q->rsrp <= 97 ? -141 + q->rsrp : 0;
	case RAT_5G_NSA:
	case RAT_5G_SA:
		return q->ss_rsrp >= 0 && q->ss_rsrp <= 127 ? -157 + q->ss_rsrp : 0;
	default:
		return 0;
	}
}

int rat_vendor_parse(const char *s, rat_vendor_t *out)
{
	if (!s)
		return -1;
	if (strcasecmp(s, "3gpp") == 0)
		*out = RAT_VENDOR_3GPP;
	else if (strcasecmp(s, "quectel") == 0)
		*out = RAT_VENDOR_QUECTEL;
	else
		return -1;
	return 0;
}

/* TS 27.007 +WS46 <n>, indexed by mask (bit0 GERAN .. bit3 NG-RAN). */
static const int ws46[16] = {
	[RAT_BIT_2G] = 12,
	[RAT_BIT_3G] = 22,
	[RAT_BIT_4G] = 28,
	[RAT_BIT_5G] = 36,
	[RAT_BIT_2G | RAT_BIT_3G] = 29,
	[RAT_BIT_2G | RAT_BIT_4G] = 30,
	[RAT_BIT_3G | RAT_BIT_4G] = 31,
	[RAT_BIT_2G | RAT_BIT_3G | RAT_BIT_4G] = 25,
	[RAT_MASK_ALL] = 35,
	[RAT_BIT_4G | RAT_BIT_5G] = 37,
	[RAT_BIT_3G | RAT_BIT_4G | RAT_BIT_5G] = 38,
	[RAT_BIT_2G | RAT_BIT_4G | RAT_BIT_5G] = 39,
	[RAT_BIT_3G | RAT_BIT_5G] = 40,
	[RAT_BIT_2G | RAT_BIT_3G | RAT_BIT_5G] = 41,
	[RAT_BIT_2G | RAT_BIT_5G] = 42,
};

int rat_pref_cmd(rat_vendor_t v, rat_mask_t m, char *buf, size_t len)
{
	static const char *const q[4] = { "GSM", "WCDMA", "LTE", "NR5G" };
	size_t off;

	m &= RAT_MASK_ALL;
	if (!m || len < 32)
		return -1;

	switch (v) {
	case RAT_VENDOR_3GPP:
		snprintf(buf, len, "AT+WS46=%d", ws46[m]);
		return 0;
	case RAT_VENDOR_QUECTEL:
		/* Order is preference order: highest RAT first. */
		off = (size_t)snprintf(buf, len, "AT+QNWPREFCFG=\"mode_pref\",");
		for (int i = 3; i >= 0 && off < len; i--) {
			if (m & (1u << i))
				off += (size_t)snprintf(buf + off, len - off, "%s%s",
							buf[off - 1] == ',' ? "" : ":", q[i]);
		}
		return off < len ? 0 : -1;
	}
	return -1;
}

void rat_policy_init(struct rat_policy *p, const struct rat_policy_cfg *cfg)
{
	memset(p, 0, sizeof(*p));
	p->cfg = *cfg;
	p->cfg.allowed &= RAT_MASK_ALL;
	if (!p->cfg.allowed)
		p->cfg.allowed = RAT_BIT_3G | RAT_BIT_4G | RAT_BIT_5G;
	for (int r = RAT_2G; r < RAT_COUNT; r++)
		if (!p->cfg.min_dbm[r])
			p->cfg.min_dbm[r] = default_min_dbm[r];
	if (!p->cfg.weak_dwell_ms)
		p->cfg.weak_dwell_ms = 30000;
	if (!p->cfg.hold_ms)
		p->cfg.hold_ms = 10 * 60000;
	if (p->cfg.max_hold_ms < p->cfg.hold_ms)
		p->cfg.max_hold_ms = p->cfg.hold_ms * 8;
	for (int d = 0; d < RAT_DOM_COUNT; d++)
		p->reg[d] = -1;
}

static bool reg_ok(int stat)
{
	return stat == AT_REG_HOME || stat == AT_REG_ROAMING;
}

/* RAT implied by a domain when the modem gives no <AcT>. */
static rat_t domain_default_rat(rat_domain_t d)
{
	return d == RAT_DOM_PS ? RAT_3G : d == RAT_DOM_EPS ? RAT_4G : RAT_5G_SA;
}

static void update_serving(struct rat_policy *p, uint64_t now)
{
	rat_t best = RAT_NONE;

	for (int d = 0; d < RAT_DOM_COUNT; d++) {
		rat_t r = p->dom_rat[d];

		if (reg_ok(p->reg[d]) && (rat_bit(r) & p->cfg.allowed) && r > best)
			best = r;
	}
	/* +COPS can tell EN-DC (NSA) apart where +CEREG still says E-UTRAN. */
	if (best != RAT_NONE && p->cops_rat > best && (rat_bit(p->cops_rat) & p->cfg.allowed))
		best = p->cops_rat;
	if (best == RAT_NONE)
		p->cops_rat = RAT_NONE;

	if (best != p->serving) {
		p->serving = best;
		p->weak = false;
		p->good_since = now;
	}
}

void rat_policy_on_reg(struct rat_policy *p, rat_domain_t d, int stat, int act,
		       uint64_t now_ms)
{
	rat_t r;

	if (d >= RAT_DOM_COUNT)
		return;
	r = rat_from_act(act);
	p->reg[d] = stat;
	p->dom_rat[d] = r != RAT_NONE ? r : domain_default_rat(d);
	update_serving(p, now_ms);
}

void rat_policy_on_cops(struct rat_policy *p, int act, uint64_t now_ms)
{
	p->cops_rat = rat_from_act(act);
	update_serving(p, now_ms);
}

int rat_policy_reg_stat(const struct rat_policy *p)
{
	bool roaming = false, denied = false, searching = false;

	for (int d = 0; d < RAT_DOM_COUNT; d++) {
		if (!(rat_bit(p->dom_rat[d]) & p->cfg.allowed))
			continue;
		switch (p->reg[d]) {
		case AT_REG_HOME:
			return AT_REG_HOME;
		case AT_REG_ROAMING:
			roaming = true;
			break;
		case AT_REG_DENIED:
			denied = true;
			break;
		case AT_REG_SEARCHING:
			searching = true;
			break;
		default:
			break;
		}
	}
	if (roaming)
		return AT_REG_ROAMING;
	/* One domain denied while another still searches: wait for it. */
	if (denied && !searching)
		return AT_REG_DENIED;
	return searching ? AT_REG_SEARCHING : AT_REG_NOT_SEARCHING;
}

/* Allowed, not excluded RATs strictly below the given one. */
static rat_mask_t fallback_below(const struct rat_policy *p, rat_mask_t bit)
{
	return (bit - 1) & p->cfg.allowed & ~p->demoted;
}

static void demote(struct rat_policy *p, rat_mask_t bit, uint64_t now)
{
	int i = bit_index(bit);
	uint64_t hold;

	if (i < 0 || !fallback_below(p, bit))
		return; /* never exclude the last usable RAT */
	hold = p->cfg.hold_ms;
	for (uint32_t n = 0; n < p->demote_count[i] && hold < p->cfg.max_hold_ms; n++)
		hold <<= 1;
	if (hold > p->cfg.max_hold_ms)
		hold = p->cfg.max_hold_ms;
	p->demoted |= bit;
	p->demote_until[i] = now + hold;
	if (p->demote_count[i] < 32)
		p->demote_count[i]++;
	p->weak = false;
}

static void eval_signal(struct rat_policy *p, uint64_t now)
{
	rat_t s = p->serving;
	int dbm = s != RAT_NONE ? p->dbm[s] : 0;
	int min = s != RAT_NONE ? p->cfg.min_dbm[s] : 0;

	if (s == RAT_NONE || !dbm) {
		p->weak = false;
		return;
	}
	if (dbm < min) {
		if (!p->weak) {
			p->weak = true;
			p->weak_since = now;
		} else if (now - p->weak_since >= p->cfg.weak_dwell_ms) {
			demote(p, rat_bit(s), now);
		}
	} else if (dbm >= min + RAT_HYST_DB) {
		if (p->weak)
			p->good_since = now;
		p->weak = false;
	}
}

void rat_policy_on_cesq(struct rat_policy *p, const struct at_cesq *q, uint64_t now_ms)
{
	for (int r = RAT_2G; r < RAT_COUNT; r++)
		p->dbm[r] = rat_signal_dbm(q, (rat_t)r);
	eval_signal(p, now_ms);
}

void rat_policy_on_data_fail(struct rat_policy *p, uint64_t now_ms)
{
	if (p->serving != RAT_NONE)
		demote(p, rat_bit(p->serving), now_ms);
}

void rat_policy_tick(struct rat_policy *p, uint64_t now_ms)
{
	for (int i = 0; i < 4; i++) {
		rat_mask_t bit = 1u << i;

		if ((p->demoted & bit) && now_ms >= p->demote_until[i])
			p->demoted &= ~bit; /* hold over: let the modem try it again */
	}
	eval_signal(p, now_ms);

	/* A RAT that has served well for a full hold period is forgiven. */
	if (p->serving != RAT_NONE && !p->weak && now_ms - p->good_since >= p->cfg.hold_ms) {
		int i = bit_index(rat_bit(p->serving));

		if (i >= 0)
			p->demote_count[i] = 0;
	}
}

rat_mask_t rat_policy_pref(const struct rat_policy *p)
{
	rat_mask_t m = p->cfg.allowed & ~p->demoted;

	return m ? m : p->cfg.allowed;
}

bool rat_policy_take_pref(struct rat_policy *p, rat_mask_t *m)
{
	rat_mask_t cur = rat_policy_pref(p);

	if (p->applied_valid && cur == p->applied)
		return false;
	p->applied = cur;
	p->applied_valid = true;
	*m = cur;
	return true;
}
