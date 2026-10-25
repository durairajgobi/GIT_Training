/* SPDX-License-Identifier: MIT */
#ifndef TCU_HFP_CODEC_H
#define TCU_HFP_CODEC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * HFP 1.7 Hands-Free side codec negotiation and SCO/eSCO parameter
 * selection for wideband speech (mSBC).
 *
 * Flow (HFP 1.7 section 4.11.3):
 *   HF -> AG  AT+BAC=1,2         (available codecs)
 *   AG -> HF  +BCS: 2            (codec selection)
 *   HF -> AG  AT+BCS=2           (confirm) or AT+BAC=... (reject)
 *   AG sets up eSCO with T2 settings for mSBC, falling back to T1.
 *
 * Fix shipped in the field: some phone/controller pairs accept mSBC but
 * fail to open eSCO with T1/T2. After a failed mSBC setup we drop mSBC
 * from AT+BAC for that phone so the next negotiation lands on CVSD,
 * instead of leaving the call with no audio.
 */

#define HFP_CODEC_CVSD 1
#define HFP_CODEC_MSBC 2

/* HF supported features (AT+BRSF), HFP 1.7 */
#define HFP_HF_EC_NR             (1u << 0)
#define HFP_HF_3WAY              (1u << 1)
#define HFP_HF_CLI               (1u << 2)
#define HFP_HF_VOICE_RECOG       (1u << 3)
#define HFP_HF_REMOTE_VOLUME     (1u << 4)
#define HFP_HF_ENH_CALL_STATUS   (1u << 5)
#define HFP_HF_ENH_CALL_CONTROL  (1u << 6)
#define HFP_HF_CODEC_NEGOTIATION (1u << 7)
#define HFP_HF_HF_INDICATORS     (1u << 8)
#define HFP_HF_ESCO_S4           (1u << 9)

/* AG supported features (+BRSF) bit for codec negotiation */
#define HFP_AG_CODEC_NEGOTIATION (1u << 9)
#define HFP_AG_ESCO_S4           (1u << 11)

struct hfp_sco_params {
	const char *name;    /* "T2", "T1", "S4", "S3", "D1" */
	uint16_t pkt_types;  /* HCI packet type bitmask (EV3, 2-EV3, ...) */
	uint16_t max_latency_ms;
	uint8_t retrans;     /* 0 none, 1 power, 2 quality */
	bool transparent;    /* mSBC: transparent air coding */
};

struct hfp_codec_ctx {
	bool controller_wbs;   /* controller supports transparent eSCO */
	bool ag_codec_neg;
	bool ag_esco_s4;
	bool msbc_blacklisted; /* per-phone, persisted by btmgr */
	int selected;          /* codec in use, 0 = none */
	unsigned int setup_try;/* index into the params ladder */
};

void hfp_codec_init(struct hfp_codec_ctx *c, bool controller_wbs);
uint32_t hfp_hf_features(const struct hfp_codec_ctx *c);
void hfp_on_ag_features(struct hfp_codec_ctx *c, uint32_t ag_features);
/* Build "AT+BAC=..." into buf; returns length or -1. */
int hfp_build_bac(const struct hfp_codec_ctx *c, char *buf, size_t len);
/* AG sent +BCS:<id>; writes AT+BCS=<id> or AT+BAC=... into buf. */
int hfp_on_bcs(struct hfp_codec_ctx *c, int codec, char *buf, size_t len);
/* Next eSCO parameter set to try, or NULL when the ladder is exhausted. */
const struct hfp_sco_params *hfp_next_sco_params(struct hfp_codec_ctx *c);
/* Report an eSCO setup failure on the selected codec. Returns true if
 * the caller should re-negotiate (mSBC was blacklisted). */
bool hfp_on_sco_failed(struct hfp_codec_ctx *c);

#endif
