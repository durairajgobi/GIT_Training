// SPDX-License-Identifier: MIT
#include "at_chan.h"
#include "check.h"

int main(void)
{
	struct at_chan c;

	at_chan_reset(&c);
	CHECK_EQ(at_chan_pop(&c), AT_OWNER_NONE);

	/* Interleaved users: results route back in send order. */
	CHECK(at_chan_push(&c, AT_OWNER_SM));
	CHECK(at_chan_push(&c, AT_OWNER_AUX));
	CHECK(at_chan_push(&c, AT_OWNER_AUX));
	CHECK(at_chan_push(&c, AT_OWNER_SM));
	CHECK_EQ(at_chan_pending(&c, AT_OWNER_AUX), 2);
	CHECK_EQ(at_chan_pop(&c), AT_OWNER_SM);
	CHECK_EQ(at_chan_pop(&c), AT_OWNER_AUX);
	CHECK_EQ(at_chan_pop(&c), AT_OWNER_AUX);
	CHECK_EQ(at_chan_pending(&c, AT_OWNER_AUX), 0);
	CHECK_EQ(at_chan_pop(&c), AT_OWNER_SM);
	CHECK_EQ(at_chan_pop(&c), AT_OWNER_NONE);

	/* Wraps around and refuses overflow. */
	for (int i = 0; i < AT_CHAN_DEPTH; i++)
		CHECK(at_chan_push(&c, i & 1 ? AT_OWNER_AUX : AT_OWNER_SM));
	CHECK(!at_chan_push(&c, AT_OWNER_SM));
	for (int i = 0; i < AT_CHAN_DEPTH; i++)
		CHECK_EQ(at_chan_pop(&c), i & 1 ? AT_OWNER_AUX : AT_OWNER_SM);

	at_chan_push(&c, AT_OWNER_AUX);
	at_chan_reset(&c);
	CHECK_EQ(at_chan_pending(&c, AT_OWNER_AUX), 0);
	CHECK_DONE();
}
