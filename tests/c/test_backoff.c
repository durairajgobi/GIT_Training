// SPDX-License-Identifier: MIT
#include "backoff.h"
#include "check.h"

int main(void)
{
	backoff_t b;

	/* No jitter: exact doubling, capped. */
	backoff_init(&b, 1000, 8000, 0, 1);
	CHECK_EQ(backoff_next(&b), 1000);
	CHECK_EQ(backoff_next(&b), 2000);
	CHECK_EQ(backoff_next(&b), 4000);
	CHECK_EQ(backoff_next(&b), 8000);
	CHECK_EQ(backoff_next(&b), 8000);
	backoff_reset(&b);
	CHECK_EQ(backoff_next(&b), 1000);

	/* Huge attempt counts must not overflow. */
	b.attempt = 4000000000u;
	CHECK_EQ(backoff_next(&b), 8000);

	/* Jitter stays inside +/- 20 %. */
	backoff_init(&b, 10000, 10000, 20, 42);
	for (int i = 0; i < 1000; i++) {
		uint32_t d = backoff_next(&b);

		CHECK(d >= 8000 && d <= 12000);
	}
	CHECK_DONE();
}
