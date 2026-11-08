/* SPDX-License-Identifier: MIT */
#ifndef TCU_RAT_H
#define TCU_RAT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "at.h"

/*
 * Multi-RAT support for the cellular connection manager: 2G (GSM/EGPRS),
 * 3G (UMTS/HSPA), 4G (LTE) and 5G (NR, NSA and SA).
 *
 * The modem does the actual cell selection and handover. This module
 *  - turns the registration URCs of every domain (+CGREG for 2G/3G PS,
 *    +CEREG for EPS, +C5GREG for 5GS) into one "registered / not" answer
 *    for lte_sm, so the bearer stays up whichever RAT carries it;
 *  - tracks the serving RAT and its signal (+COPS?, +CESQ);
 *  - steers the modem's RAT preference: a RAT whose signal stays below
 *    its threshold, or whose data bearer fails, is excluded for a hold
 *    time (doubling on repeat, capped) and then retried. This stops the
 *    5G/LTE or LTE/3G ping-pong seen at cell edges.
 *
 * Pure logic: time is passed in by the caller, no I/O.
 */

typedef enum {
	RAT_NONE,
	RAT_2G,
	RAT_3G,
	RAT_4G,
	RAT_5G_NSA, /* E-UTRA-NR dual connectivity, LTE anchor */
	RAT_5G_SA,
	RAT_COUNT
} rat_t;

/* Preference mask. 5G covers both NSA and SA. */
typedef unsigned int rat_mask_t;
#define RAT_BIT_2G  0x1u
#define RAT_BIT_3G  0x2u
#define RAT_BIT_4G  0x4u
#define RAT_BIT_5G  0x8u
#define RAT_MASK_ALL 0xfu

const char *rat_name(rat_t r);
rat_mask_t rat_bit(rat_t r);

/* TS 27.007 <AcT> (from +COPS / +CxREG) to RAT; RAT_NONE if unknown. */
rat_t rat_from_act(int act);

/* "2g,3g,4g,5g" (any subset, any order) or "all". Returns 0 or -1. */
int rat_mask_parse(const char *s, rat_mask_t *out);
/* "5G/4G/3G" style, highest first; "-" for an empty mask. */
const char *rat_mask_str(rat_mask_t m, char *buf, size_t len);

/* Signal strength in dBm for one RAT from +CESQ; 0 if not reported. */
int rat_signal_dbm(const struct at_cesq *q, rat_t r);

/* How the RAT preference is written to the modem. */
typedef enum {
	RAT_VENDOR_3GPP,    /* AT+WS46=<n>  (TS 27.007 §5.9) */
	RAT_VENDOR_QUECTEL, /* AT+QNWPREFCFG="mode_pref",NR5G:LTE:WCDMA:GSM */
} rat_vendor_t;

int rat_vendor_parse(const char *s, rat_vendor_t *out);
/* Build the preference command for a mask. Returns 0 or -1 (empty mask). */
int rat_pref_cmd(rat_vendor_t v, rat_mask_t m, char *buf, size_t len);

typedef enum {
	RAT_DOM_PS,  /* +CGREG: GERAN / UTRAN packet domain */
	RAT_DOM_EPS, /* +CEREG */
	RAT_DOM_5GS, /* +C5GREG */
	RAT_DOM_COUNT
} rat_domain_t;

struct rat_policy_cfg {
	rat_mask_t allowed;          /* RATs we may ever use */
	int min_dbm[RAT_COUNT];      /* weak-signal threshold, 0 = default */
	uint32_t weak_dwell_ms;      /* below threshold this long -> demote */
	uint32_t hold_ms;            /* first exclusion period */
	uint32_t max_hold_ms;        /* cap for repeated exclusions */
};

struct rat_policy {
	struct rat_policy_cfg cfg;
	int reg[RAT_DOM_COUNT];      /* last <stat>, -1 = never reported */
	rat_t dom_rat[RAT_DOM_COUNT];
	rat_t cops_rat;
	rat_t serving;
	int dbm[RAT_COUNT];          /* 0 = unknown */

	bool weak;
	uint64_t weak_since;
	uint64_t good_since;

	rat_mask_t demoted;
	uint64_t demote_until[4];    /* per mask bit */
	uint32_t demote_count[4];

	rat_mask_t applied;          /* last preference handed out */
	bool applied_valid;
};

void rat_policy_init(struct rat_policy *p, const struct rat_policy_cfg *cfg);

/* Feed a +CGREG/+CEREG/+C5GREG report (act = -1 if absent). */
void rat_policy_on_reg(struct rat_policy *p, rat_domain_t d, int stat, int act,
		       uint64_t now_ms);
void rat_policy_on_cops(struct rat_policy *p, int act, uint64_t now_ms);
void rat_policy_on_cesq(struct rat_policy *p, const struct at_cesq *q, uint64_t now_ms);
/* The data bearer could not be brought up on the serving RAT. */
void rat_policy_on_data_fail(struct rat_policy *p, uint64_t now_ms);
/* Periodic housekeeping: weak-signal timer and exclusion expiry. */
void rat_policy_tick(struct rat_policy *p, uint64_t now_ms);

/* Combined registration over all domains, as an enum at_reg_stat. */
int rat_policy_reg_stat(const struct rat_policy *p);

/* The preference the modem should currently use. Never empty. */
rat_mask_t rat_policy_pref(const struct rat_policy *p);
/* True (and *m set) when the preference differs from the last one taken. */
bool rat_policy_take_pref(struct rat_policy *p, rat_mask_t *m);

#endif
