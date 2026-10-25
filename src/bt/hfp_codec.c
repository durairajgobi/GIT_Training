// SPDX-License-Identifier: MIT
#include "hfp_codec.h"

#include <stdio.h>

/* HCI eSCO packet type bits (Core spec, "Setup Synchronous Connection").
 * The EDR bits are "may NOT be used" flags, hence the inverted sense. */
#define PKT_HV3    0x0004
#define PKT_EV3    0x0008
#define PKT_NO_2EV3 0x0040
#define PKT_NO_3EV3 0x0080
#define PKT_NO_2EV5 0x0100
#define PKT_NO_3EV5 0x0200

/* HFP 1.7 table 5.8 */
static const struct hfp_sco_params msbc_ladder[] = {
	{ "T2", PKT_EV3 | PKT_NO_3EV3 | PKT_NO_2EV5 | PKT_NO_3EV5, 13, 2, true },
	{ "T1", PKT_EV3 | PKT_NO_2EV3 | PKT_NO_3EV3 | PKT_NO_2EV5 | PKT_NO_3EV5, 8, 2, true },
};

static const struct hfp_sco_params cvsd_ladder_s4[] = {
	{ "S4", PKT_EV3 | PKT_NO_3EV3 | PKT_NO_2EV5 | PKT_NO_3EV5, 12, 2, false },
	{ "S3", PKT_EV3 | PKT_NO_3EV3 | PKT_NO_2EV5 | PKT_NO_3EV5, 10, 1, false },
	{ "D1", PKT_HV3 | PKT_NO_2EV3 | PKT_NO_3EV3 | PKT_NO_2EV5 | PKT_NO_3EV5, 0xffff, 0xff, false },
};

void hfp_codec_init(struct hfp_codec_ctx *c, bool controller_wbs)
{
	*c = (struct hfp_codec_ctx){ .controller_wbs = controller_wbs };
}

static bool msbc_usable(const struct hfp_codec_ctx *c)
{
	return c->controller_wbs && c->ag_codec_neg && !c->msbc_blacklisted;
}

uint32_t hfp_hf_features(const struct hfp_codec_ctx *c)
{
	uint32_t f = HFP_HF_EC_NR | HFP_HF_3WAY | HFP_HF_CLI | HFP_HF_REMOTE_VOLUME |
		     HFP_HF_ENH_CALL_STATUS | HFP_HF_HF_INDICATORS | HFP_HF_ESCO_S4;

	if (c->controller_wbs)
		f |= HFP_HF_CODEC_NEGOTIATION;
	return f;
}

void hfp_on_ag_features(struct hfp_codec_ctx *c, uint32_t ag)
{
	c->ag_codec_neg = (ag & HFP_AG_CODEC_NEGOTIATION) != 0;
	c->ag_esco_s4 = (ag & HFP_AG_ESCO_S4) != 0;
	/* Without codec negotiation the link is CVSD by definition. */
	if (!c->ag_codec_neg)
		c->selected = HFP_CODEC_CVSD;
}

int hfp_build_bac(const struct hfp_codec_ctx *c, char *buf, size_t len)
{
	int n = msbc_usable(c) ? snprintf(buf, len, "AT+BAC=%d,%d", HFP_CODEC_CVSD, HFP_CODEC_MSBC)
			       : snprintf(buf, len, "AT+BAC=%d", HFP_CODEC_CVSD);

	return (n < 0 || (size_t)n >= len) ? -1 : n;
}

int hfp_on_bcs(struct hfp_codec_ctx *c, int codec, char *buf, size_t len)
{
	int n;

	if (codec == HFP_CODEC_CVSD || (codec == HFP_CODEC_MSBC && msbc_usable(c))) {
		c->selected = codec;
		c->setup_try = 0;
		n = snprintf(buf, len, "AT+BCS=%d", codec);
		return (n < 0 || (size_t)n >= len) ? -1 : n;
	}
	/* Unsupported selection: answer with our codec list (HFP 4.11.3). */
	return hfp_build_bac(c, buf, len);
}

const struct hfp_sco_params *hfp_next_sco_params(struct hfp_codec_ctx *c)
{
	const struct hfp_sco_params *ladder;
	unsigned int n, i = c->setup_try;

	if (c->selected == HFP_CODEC_MSBC) {
		ladder = msbc_ladder;
		n = sizeof(msbc_ladder) / sizeof(msbc_ladder[0]);
	} else {
		/* S4 is only allowed when both sides are HFP 1.7 S4 capable. */
		ladder = cvsd_ladder_s4;
		n = sizeof(cvsd_ladder_s4) / sizeof(cvsd_ladder_s4[0]);
		if (!c->ag_esco_s4 && i == 0)
			i = c->setup_try = 1;
	}
	if (i >= n)
		return NULL;
	c->setup_try++;
	return &ladder[i];
}

bool hfp_on_sco_failed(struct hfp_codec_ctx *c)
{
	if (c->selected != HFP_CODEC_MSBC)
		return false;
	/* hfp_next_sco_params() already advanced; ladder exhausted? */
	if (c->setup_try < sizeof(msbc_ladder) / sizeof(msbc_ladder[0]))
		return false;
	c->msbc_blacklisted = true;
	c->selected = 0;
	c->setup_try = 0;
	return true;
}
