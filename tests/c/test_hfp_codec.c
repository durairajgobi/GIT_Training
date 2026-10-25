// SPDX-License-Identifier: MIT
#include "check.h"
#include "hfp_codec.h"

int main(void)
{
	struct hfp_codec_ctx c;
	const struct hfp_sco_params *p;
	char buf[32];

	/* WBS-capable controller + AG with codec negotiation and S4. */
	hfp_codec_init(&c, true);
	CHECK(hfp_hf_features(&c) & HFP_HF_CODEC_NEGOTIATION);
	hfp_on_ag_features(&c, HFP_AG_CODEC_NEGOTIATION | HFP_AG_ESCO_S4);
	hfp_build_bac(&c, buf, sizeof(buf));
	CHECK_STR(buf, "AT+BAC=1,2");
	hfp_on_bcs(&c, HFP_CODEC_MSBC, buf, sizeof(buf));
	CHECK_STR(buf, "AT+BCS=2");

	/* mSBC ladder T2 -> T1, then blacklist and renegotiate to CVSD. */
	p = hfp_next_sco_params(&c);
	CHECK_STR(p->name, "T2");
	CHECK(p->transparent);
	CHECK(!hfp_on_sco_failed(&c));
	p = hfp_next_sco_params(&c);
	CHECK_STR(p->name, "T1");
	CHECK(hfp_on_sco_failed(&c));
	hfp_build_bac(&c, buf, sizeof(buf));
	CHECK_STR(buf, "AT+BAC=1");
	/* AG insists on mSBC: we answer with our codec list. */
	hfp_on_bcs(&c, HFP_CODEC_MSBC, buf, sizeof(buf));
	CHECK_STR(buf, "AT+BAC=1");
	hfp_on_bcs(&c, HFP_CODEC_CVSD, buf, sizeof(buf));
	CHECK_STR(buf, "AT+BCS=1");
	p = hfp_next_sco_params(&c);
	CHECK_STR(p->name, "S4");

	/* AG without S4: CVSD starts at S3, ends at D1, then exhausted. */
	hfp_codec_init(&c, true);
	hfp_on_ag_features(&c, 0);
	CHECK_EQ(c.selected, HFP_CODEC_CVSD);
	CHECK_STR(hfp_next_sco_params(&c)->name, "S3");
	CHECK_STR(hfp_next_sco_params(&c)->name, "D1");
	CHECK(hfp_next_sco_params(&c) == NULL);

	/* Controller without WBS never offers mSBC. */
	hfp_codec_init(&c, false);
	CHECK(!(hfp_hf_features(&c) & HFP_HF_CODEC_NEGOTIATION));
	hfp_on_ag_features(&c, HFP_AG_CODEC_NEGOTIATION);
	hfp_build_bac(&c, buf, sizeof(buf));
	CHECK_STR(buf, "AT+BAC=1");
	CHECK_DONE();
}
